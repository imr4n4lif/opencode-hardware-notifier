import test from "node:test";
import assert from "node:assert/strict";

import { createStateMachine } from "../plugin/buzzer.js";

function tracker() {
  const states = [];
  const machine = createStateMachine({
    onChange: (state) => states.push(state),
  });

  return {
    machine,
    states,
    last() {
      return states.at(-1);
    },
  };
}

const event = (type, data = {}) => ({ type, data });

test("starting a task turns the light green", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));

  assert.equal(t.last(), "WORKING");
});

test("thinking, streaming and step boundaries keep working", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));
  t.machine.handle(event("session.reasoning.started", { sessionID: "s1" }));
  t.machine.handle(event("session.reasoning.ended", { sessionID: "s1" }));
  t.machine.handle(event("session.text.started", { sessionID: "s1" }));
  t.machine.handle(event("session.text.ended", { sessionID: "s1" }));
  t.machine.handle(event("session.step.ended", { sessionID: "s1" }));

  assert.equal(t.last(), "WORKING");
  assert.ok(!t.states.includes("IDLE"));
});

test("a task only completes when execution succeeds", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));
  t.machine.handle(event("session.execution.succeeded", { sessionID: "s1" }));

  assert.equal(t.last(), "IDLE");
});

test("a subagent finishing does not complete the parent task", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "parent" }));
  t.machine.handle(event("session.execution.started", { sessionID: "child" }));
  t.machine.handle(event("session.execution.succeeded", { sessionID: "child" }));

  assert.equal(t.last(), "WORKING");
  assert.ok(!t.states.includes("IDLE"));

  t.machine.handle(event("session.execution.succeeded", { sessionID: "parent" }));

  assert.equal(t.last(), "IDLE");
});

test("a failed subagent does not turn the task red while the parent runs", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "parent" }));
  t.machine.handle(event("session.execution.started", { sessionID: "child" }));
  t.machine.handle(event("session.execution.failed", { sessionID: "child" }));

  assert.equal(t.last(), "WORKING");
  assert.ok(!t.states.includes("ERROR"));

  t.machine.handle(event("session.execution.succeeded", { sessionID: "parent" }));

  assert.equal(t.last(), "IDLE");
});

test("a failed task turns red and stays red until new work starts", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));
  t.machine.handle(event("session.execution.failed", { sessionID: "s1" }));
  t.machine.handle(event("session.idle", { sessionID: "s1" }));

  assert.equal(t.last(), "ERROR");

  t.machine.handle(event("session.execution.started", { sessionID: "s2" }));

  assert.equal(t.last(), "WORKING");
});

test("cancelling a running task returns to idle", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));
  t.machine.handle(
    event("session.execution.interrupted", { sessionID: "s1", reason: "user" }),
  );

  assert.equal(t.last(), "IDLE");
});

test("a superseded execution does not flash idle", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));
  t.machine.handle(
    event("session.execution.interrupted", {
      sessionID: "s1",
      reason: "superseded",
    }),
  );
  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));

  assert.ok(!t.states.includes("IDLE"));
  assert.equal(t.last(), "WORKING");
});

test("a user prompt turns green immediately", () => {
  const t = tracker();

  t.machine.handle(
    event("session.inbox.enqueued", {
      sessionID: "s1",
      item: { type: "user" },
    }),
  );

  assert.equal(t.last(), "WORKING");
});

test("synthetic inbox items do not wake the notifier", () => {
  const t = tracker();

  t.machine.handle(
    event("session.inbox.enqueued", {
      sessionID: "s1",
      item: { type: "synthetic" },
    }),
  );

  assert.deepEqual(t.states, []);
});

test("cancelling a queued prompt returns to idle", () => {
  const t = tracker();

  t.machine.handle(
    event("session.inbox.enqueued", {
      sessionID: "s1",
      item: { type: "user" },
    }),
  );
  t.machine.handle(event("session.inbox.cancelled", { sessionID: "s1" }));

  assert.equal(t.last(), "IDLE");
});

test("permissions and forms show the prompt state and resume working", () => {
  const t = tracker();

  t.machine.handle(event("session.execution.started", { sessionID: "s1" }));
  t.machine.handle(event("permission.asked", { sessionID: "s1" }));

  assert.equal(t.last(), "PROMPT");

  t.machine.handle(event("permission.replied", { sessionID: "s1" }));

  assert.equal(t.last(), "WORKING");

  t.machine.handle(event("form.created", { form: { sessionID: "s1" } }));

  assert.equal(t.last(), "PROMPT");

  t.machine.handle(event("form.cancelled", { sessionID: "s1" }));

  assert.equal(t.last(), "WORKING");
});

test("an unrelated idle session cannot turn the light off", () => {
  const t = tracker();

  t.machine.handle(event("session.idle", { sessionID: "background" }));

  assert.deepEqual(t.states, []);
});
