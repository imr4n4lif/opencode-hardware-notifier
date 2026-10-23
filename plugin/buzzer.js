import { appendFile, open } from "node:fs/promises";
import { execFile } from "node:child_process";
import { promisify } from "node:util";

const execFileAsync = promisify(execFile);

const LOG_FILE = "/tmp/opencode-buzzer-debug.log";

async function log(message) {
  try {
    await appendFile(LOG_FILE, `${new Date().toISOString()} ${message}\n`);
  } catch (e) {
    // ignore
  }
  console.log(message);
}

// Events that mean OpenCode is actively doing something. Thinking, streaming,
// tool calls and subagents all count as WORKING; none of them mean the task is
// finished.
const WORKING_EVENTS = new Set([
  "session.step.started",
  "session.text.started",
  "session.reasoning.started",
  "session.tool.input.started",
  "session.tool.called",
  "session.tool.progress",
  "session.shell.started",
  "session.compaction.started",
  "session.retry.scheduled",
]);

// Events that mean OpenCode is waiting for the user.
const PROMPT_EVENTS = new Set(["permission.asked", "form.created"]);

// Replies resume the task, so the green working state should come back.
const PROMPT_REPLY_EVENTS = new Set([
  "permission.replied",
  "form.replied",
  "form.cancelled",
]);

/**
 * Fold OpenCode events into the hardware states.
 *
 * The tracker keeps a set of sessions that are currently executing. A child
 * session (subagent) finishing does not end the task while its parent is still
 * running, so IDLE is only emitted once every execution has ended.
 */
export function createStateMachine({ onChange = () => {} } = {}) {
  const activeSessions = new Set();
  const knownSessions = new Set();
  let latchedError = false;

  function working() {
    latchedError = false;
    onChange("WORKING");
  }

  function finishIfIdle(state) {
    if (activeSessions.size > 0) return;
    // A failure should stay visible until new work actually starts.
    if (state === "IDLE" && latchedError) return;
    onChange(state);
  }

  function handle(event) {
    const type = String(event?.type ?? "").toLowerCase();
    const data = event?.data ?? {};
    const sessionID = data.sessionID ?? data.form?.sessionID ?? null;

    if (WORKING_EVENTS.has(type)) {
      working();
      return;
    }

    if (PROMPT_EVENTS.has(type)) {
      onChange("PROMPT");
      return;
    }

    if (PROMPT_REPLY_EVENTS.has(type)) {
      working();
      return;
    }

    switch (type) {
      // A prompt was accepted: give immediate feedback before the model
      // starts thinking. Synthetic/compaction inbox items are internal work.
      case "session.inbox.enqueued":
        if (data.item?.type === "user") working();
        return;

      case "session.inbox.delivered":
        working();
        return;

      case "session.inbox.cancelled":
        if (sessionID) activeSessions.delete(sessionID);
        finishIfIdle("IDLE");
        return;

      case "session.execution.started":
        if (sessionID) {
          activeSessions.add(sessionID);
          knownSessions.add(sessionID);
        }
        working();
        return;

      case "session.execution.succeeded":
        if (sessionID) activeSessions.delete(sessionID);
        finishIfIdle("IDLE");
        return;

      case "session.execution.failed":
        if (sessionID) activeSessions.delete(sessionID);
        if (activeSessions.size === 0) {
          latchedError = true;
          onChange("ERROR");
        } else {
          // A subagent failed but the main task is still running; the parent
          // gets to decide whether that is fatal.
          working();
        }
        return;

      case "session.execution.interrupted":
        if (sessionID) activeSessions.delete(sessionID);
        // "superseded" means a newer prompt replaced this run. Do not flash
        // IDLE in between; the new execution is about to start.
        if (data.reason === "superseded") return;
        finishIfIdle("IDLE");
        return;

      // Fallback in case a completion event is missed. Only sessions that we
      // saw start are considered, so unrelated idle sessions cannot turn the
      // light yellow at startup.
      case "session.idle":
        if (sessionID && knownSessions.has(sessionID)) {
          knownSessions.delete(sessionID);
          activeSessions.delete(sessionID);
          finishIfIdle("IDLE");
        }
        return;

      // Defensive: older payloads/custom plugins may emit this.
      case "session.error":
        if (activeSessions.size === 0) {
          latchedError = true;
          onChange("ERROR");
        }
        return;

      default:
        return;
    }
  }

  return {
    handle,
    get activeSessions() {
      return [...activeSessions];
    },
  };
}

export default {
  id: "hardware-notifier",

  async setup(ctx) {
    await log("[hardware-notifier] setup() started");

    const PORT = "/dev/ttyUSB0";
    const BAUD = "9600";

    let serial = null;
    let lastState = null;
    let writeQueue = Promise.resolve();

    async function sendState(state) {
      if (!serial || state === lastState) return;

      lastState = state;

      writeQueue = writeQueue.catch(() => {}).then(async () => {
        await serial.write(`${state}\n`);
        await log(`[hardware-notifier] Sent ${state}`);
      });

      try {
        await writeQueue;
      } catch (error) {
        lastState = null;
        await log(
          "[hardware-notifier] Serial write failed: " + error.message
        );
      }
    }

    try {
      await execFileAsync("stty", ["-F", PORT, BAUD, "raw", "-echo"]);

      serial = await open(PORT, "r+");

      // Many Arduino boards reset when the serial port opens.
      await new Promise((resolve) => setTimeout(resolve, 2000));

      await log(`[hardware-notifier] Connected to ${PORT}`);
      await sendState("READY");
    } catch (error) {
      await log("[hardware-notifier] Serial setup failed: " + error.message);
    }

    const controller = new AbortController();
    const machine = createStateMachine({ onChange: sendState });

    void (async () => {
      try {
        for await (const event of ctx.event.subscribe({
          signal: controller.signal,
        })) {
          await log(
            `[hardware-notifier] EVENT type=${event.type}` +
              (event.data?.sessionID
                ? ` session=${event.data.sessionID}`
                : "")
          );

          machine.handle(event);
        }
      } catch (error) {
        if (!controller.signal.aborted) {
          await log("[hardware-notifier] Event stream failed: " + error);
        }
      }
    })();

    return async () => {
      controller.abort();

      // Clear the hardware before releasing the port so the Arduino does not
      // stay lit after OpenCode closes or the plugin unloads.
      await sendState("OFF");
      await writeQueue.catch(() => {});

      if (serial) {
        await serial.close();
        serial = null;
      }
    };
  },
};
