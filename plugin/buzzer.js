import { open } from "node:fs/promises";
import { execFile } from "node:child_process";
import { appendFile } from "node:fs/promises";
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

export default {
  id: "hardware-notifier",

  async setup(ctx) {
    await log("[hardware-notifier] setup() started");

    const PORT = "/dev/ttyUSB0";
    const BAUD = "9600";

    let serial;

    try {
      await execFileAsync("stty", [
        "-F", PORT, BAUD, "raw", "-echo",
      ]);

      serial = await open(PORT, "r+");

      await new Promise((resolve) => setTimeout(resolve, 2000));

      await log(`[hardware-notifier] Connected to ${PORT}`);
      await sendState("READY");
    } catch (error) {
      await log(
        "[hardware-notifier] Serial setup failed: " + error.message
      );
    }

    let lastState = null;
    let writeQueue = Promise.resolve();

    async function sendState(state) {
      if (!serial || state === lastState) return;

      lastState = state;

      writeQueue = writeQueue.then(async () => {
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

    const controller = new AbortController();

    void (async () => {
      try {
        for await (const event of ctx.event.subscribe({
          signal: controller.signal,
        })) {
          await log(
            `[hardware-notifier] EVENT type=${event.type}`
          );

          const et = event.type.toLowerCase();

          // === IDLE (cancelled / interrupted / stopped / removed) ===
          // Catch any cancel/stop/interrupt event regardless of exact name
          if (
            et.includes("cancel") ||
            et.includes("canceled") ||
            et.includes("cancelled") ||
            et.includes("interrupt") ||
            et.includes("stop") ||
            et.includes("inbox.remove") ||
            et.includes("inbox.delete") ||
            et.includes("message.remove") ||
            et.includes("message.delete")
          ) {
            await sendState("IDLE");
            continue;
          }

          // === ERROR ===
          if (et.includes("failed") || et.includes("error")) {
            await sendState("ERROR");
            continue;
          }

          // === WORKING ===
          // Immediate feedback when user sends a prompt (before thinking starts)
          if (
            et.includes("inbox.enqueued") ||
            et.includes("inbox.delivered") ||
            et.includes("execution.started") ||
            et.includes("step.started") ||
            et.includes("text.started") ||
            et.includes("reasoning.started")
          ) {
            await sendState("WORKING");
            continue;
          }

          // === IDLE (done naturally) ===
          if (
            et.includes("execution.succeeded") ||
            et.includes("step.ended") ||
            et.includes("text.ended") ||
            et.includes("reasoning.ended") ||
            et.includes("execution.completed") ||
            et.includes("step.completed")
          ) {
            await sendState("IDLE");
            continue;
          }

          // Ignore background events
        }
      } catch (error) {
        if (!controller.signal.aborted) {
          await log(
            "[hardware-notifier] Event stream failed: " + error
          );
        }
      }
    })();

    return async () => {
      controller.abort();
      await writeQueue.catch(() => {});
      if (serial) await serial.close();
    };
  },
};
