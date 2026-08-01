"use client";

import {
  ChangeEvent,
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";
import {
  MachineSnapshot,
  MemorySnapshot,
  RvemuBrowser,
} from "../lib/rvemu";

const terminalStatuses = new Set(["exited", "trapped", "error", "breakpoint"]);

function parseAddress(value: string): number {
  const text = value.trim();
  if (!text) throw new Error("Enter an address first.");
  const parsed = text.toLowerCase().startsWith("0x")
    ? Number.parseInt(text.slice(2), 16)
    : Number.parseInt(text, 10);
  if (!Number.isFinite(parsed) || parsed < 0 || parsed > 0xffffffff) {
    throw new Error("Address must be a 32-bit value.");
  }
  return parsed >>> 0;
}

function percent(part: number, total: number): string {
  if (total === 0) return "—";
  return `${((part / total) * 100).toFixed(1)}%`;
}

function number(value: number | null | undefined): string {
  if (value == null) return "—";
  return new Intl.NumberFormat("en-US").format(value);
}

function statusLabel(snapshot: MachineSnapshot | null, running: boolean) {
  if (running) return "Running";
  if (!snapshot) return "Booting";
  return snapshot.status.charAt(0).toUpperCase() + snapshot.status.slice(1);
}

export default function Home() {
  const emulatorRef = useRef<RvemuBrowser | null>(null);
  const fileInputRef = useRef<HTMLInputElement | null>(null);
  const frameRef = useRef<number | null>(null);
  const runningRef = useRef(false);
  const [snapshot, setSnapshot] = useState<MachineSnapshot | null>(null);
  const [memory, setMemory] = useState<MemorySnapshot | null>(null);
  const [fileName, setFileName] = useState("rv32im-dashboard.elf");
  const [inspectAddress, setInspectAddress] = useState("0x80000000");
  const [breakpointAddress, setBreakpointAddress] = useState("0x80000028");
  const [consoleMode, setConsoleMode] = useState<"all" | "stdout" | "stderr">(
    "all",
  );
  const [running, setRunning] = useState(false);
  const [notice, setNotice] = useState<string | null>(null);

  const inspect = useCallback((addressText?: string) => {
    const emulator = emulatorRef.current;
    if (!emulator) return;
    try {
      const address = parseAddress(addressText ?? inspectAddress);
      setMemory(emulator.readMemory(address, 64));
      setInspectAddress(`0x${address.toString(16).padStart(8, "0")}`);
      setNotice(null);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Memory read failed.");
    }
  }, [inspectAddress]);

  const stop = useCallback(() => {
    runningRef.current = false;
    setRunning(false);
    if (frameRef.current != null) {
      window.cancelAnimationFrame(frameRef.current);
      frameRef.current = null;
    }
  }, []);

  const installSnapshot = useCallback(
    (next: MachineSnapshot, refreshMemory = false) => {
      setSnapshot(next);
      setNotice(null);
      if (refreshMemory && next.load && emulatorRef.current) {
        try {
          const address = parseAddress(inspectAddress);
          setMemory(emulatorRef.current.readMemory(address, 64));
        } catch {
          // The visible execution result remains useful if the inspected range
          // is no longer valid for a newly loaded image.
        }
      }
    },
    [inspectAddress],
  );

  const loadBytes = useCallback(
    (bytes: Uint8Array, name: string) => {
      const emulator = emulatorRef.current;
      if (!emulator) return;
      stop();
      try {
        const next = emulator.loadElf(bytes);
        setFileName(name);
        setSnapshot(next);
        const address = next.load?.rangeBegin ?? next.pcHex;
        setInspectAddress(address);
        setMemory(emulator.readMemory(parseAddress(address), 64));
        setNotice(null);
      } catch (error) {
        setNotice(error instanceof Error ? error.message : "ELF load failed.");
      }
    },
    [stop],
  );

  useEffect(() => {
    let cancelled = false;
    async function boot() {
      try {
        const emulator = await RvemuBrowser.create();
        if (cancelled) return;
        emulatorRef.current = emulator;
        const response = await fetch("/demo/rv32im-dashboard.elf");
        if (!response.ok) throw new Error("The bundled demo ELF is unavailable.");
        const bytes = new Uint8Array(await response.arrayBuffer());
        if (cancelled) return;
        loadBytes(bytes, "rv32im-dashboard.elf");
      } catch (error) {
        if (!cancelled) {
          setNotice(
            error instanceof Error
              ? error.message
              : "The browser emulator could not start.",
          );
        }
      }
    }
    void boot();
    return () => {
      cancelled = true;
      runningRef.current = false;
      if (frameRef.current != null) {
        window.cancelAnimationFrame(frameRef.current);
      }
    };
  }, [loadBytes]);

  const step = useCallback(() => {
    const emulator = emulatorRef.current;
    if (!emulator || !snapshot?.ready) return;
    stop();
    try {
      installSnapshot(emulator.step(), true);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Step failed.");
    }
  }, [installSnapshot, snapshot?.ready, stop]);

  const run = useCallback(() => {
    const emulator = emulatorRef.current;
    if (!emulator || !snapshot?.ready || runningRef.current) return;
    runningRef.current = true;
    setRunning(true);
    setNotice(null);

    const advance = () => {
      if (!runningRef.current || !emulatorRef.current) return;
      try {
        const next = emulatorRef.current.runBatch(600);
        setSnapshot(next);
        if (terminalStatuses.has(next.status)) {
          stop();
          try {
            setMemory(
              emulatorRef.current.readMemory(parseAddress(inspectAddress), 64),
            );
          } catch {
            // Keep the terminal execution state visible.
          }
          return;
        }
        frameRef.current = window.requestAnimationFrame(advance);
      } catch (error) {
        stop();
        setNotice(error instanceof Error ? error.message : "Run failed.");
      }
    };
    frameRef.current = window.requestAnimationFrame(advance);
  }, [inspectAddress, snapshot?.ready, stop]);

  const reset = useCallback(() => {
    if (!emulatorRef.current) return;
    stop();
    try {
      const next = emulatorRef.current.reset();
      installSnapshot(next);
      const address = next.load?.rangeBegin ?? next.pcHex;
      setInspectAddress(address);
      setMemory(emulatorRef.current.readMemory(parseAddress(address), 64));
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Reset failed.");
    }
  }, [installSnapshot, stop]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      if (event.key === "F10") {
        event.preventDefault();
        step();
      } else if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
        event.preventDefault();
        if (runningRef.current) {
          stop();
        } else {
          run();
        }
      } else if (event.key === "Escape") {
        stop();
      }
    };
    window.addEventListener("keydown", onKeyDown);
    return () => window.removeEventListener("keydown", onKeyDown);
  }, [run, step, stop]);

  const onFile = async (event: ChangeEvent<HTMLInputElement>) => {
    const file = event.target.files?.[0];
    if (!file) return;
    if (file.size > 32 * 1024 * 1024) {
      setNotice("Choose an ELF smaller than 32 MiB for the browser demo.");
      return;
    }
    loadBytes(new Uint8Array(await file.arrayBuffer()), file.name);
    event.target.value = "";
  };

  const toggleBreakpoint = (addressText: string, enabled: boolean) => {
    if (!emulatorRef.current) return;
    try {
      const address = parseAddress(addressText);
      if ((address & 3) !== 0) {
        throw new Error("Breakpoints must be four-byte aligned.");
      }
      installSnapshot(emulatorRef.current.setBreakpoint(address, enabled));
      setBreakpointAddress(`0x${address.toString(16).padStart(8, "0")}`);
    } catch (error) {
      setNotice(error instanceof Error ? error.message : "Invalid breakpoint.");
    }
  };

  const memoryRows = useMemo(() => {
    if (!memory) return [];
    const start = parseAddress(memory.address);
    return Array.from({ length: Math.ceil(memory.bytes.length / 16) }, (_, row) => {
      const bytes = memory.bytes.slice(row * 16, row * 16 + 16);
      return {
        address: `0x${((start + row * 16) >>> 0).toString(16).padStart(8, "0")}`,
        bytes,
        ascii: bytes
          .map((byte) => (byte >= 32 && byte <= 126 ? String.fromCharCode(byte) : "·"))
          .join(""),
      };
    });
  }, [memory]);

  const stdout = snapshot?.stdout ?? "";
  const stderr = snapshot?.stderr ?? "";
  const consoleText =
    consoleMode === "stdout"
      ? stdout
      : consoleMode === "stderr"
        ? stderr
        : `${stdout}${stderr}`;
  const stats = snapshot?.statistics;
  const instructionCache = stats?.instructionCache;
  const dataCache = stats?.dataCache;
  const branches = stats?.branches;
  const cpi =
    stats?.estimatedCycles != null && stats.retired > 0
      ? (stats.estimatedCycles / stats.retired).toFixed(2)
      : "—";
  const canExecute = Boolean(
    snapshot?.ready && !["exited", "trapped", "error"].includes(snapshot.status),
  );

  return (
    <main className="app-shell">
      <header className="topbar">
        <div className="brand-lockup">
          <div className="brand-mark" aria-hidden="true">
            RV
          </div>
          <div>
            <div className="brand-line">
              <span>RVEMU</span>
              <span className="brand-slash">/</span>
              <span className="brand-muted">BROWSER LAB</span>
            </div>
            <p>Correctness-first RV32IM execution, visible.</p>
          </div>
        </div>
        <div className="topbar-actions">
          <div className="isa-pill">RV32IM · ILP32 · IALIGN=32</div>
          <input
            ref={fileInputRef}
            className="visually-hidden"
            type="file"
            accept=".elf,application/octet-stream"
            onChange={onFile}
            aria-label="Load a RISC-V ELF file"
          />
          <button className="button button-quiet" onClick={() => fileInputRef.current?.click()}>
            <span aria-hidden="true">↑</span> Load ELF
          </button>
        </div>
      </header>

      <section className="control-deck" aria-label="Execution controls">
        <div className="machine-state">
          <span className={`status-light status-${running ? "running" : snapshot?.status ?? "empty"}`} />
          <div>
            <span className="eyebrow">Machine state</span>
            <strong>{statusLabel(snapshot, running)}</strong>
          </div>
        </div>
        <div className="instruction-focus">
          <div className="pc-readout">
            <span className="eyebrow">Program counter</span>
            <strong>{snapshot?.pcHex ?? "0x————————"}</strong>
          </div>
          <div className="instruction-divider" />
          <div className="assembly-readout">
            <span className="eyebrow">Next instruction</span>
            <strong>
              <b>{snapshot?.instruction?.mnemonic ?? "waiting"}</b>{" "}
              <span>{snapshot?.instruction?.operands ?? "for WebAssembly"}</span>
            </strong>
          </div>
          <div className="raw-readout">
            <span className="eyebrow">Encoding</span>
            <code>{snapshot?.instruction?.rawHex ?? "0x————————"}</code>
          </div>
        </div>
        <div className="transport">
          <button className="icon-button" onClick={reset} disabled={!snapshot?.ready} title="Reset">
            ↺<span className="visually-hidden">Reset</span>
          </button>
          <button className="button button-secondary" onClick={step} disabled={!canExecute || running}>
            Step <kbd>F10</kbd>
          </button>
          {running ? (
            <button className="button button-primary" onClick={stop}>
              <span className="pause-glyph" aria-hidden="true">Ⅱ</span> Pause
            </button>
          ) : (
            <button className="button button-primary" onClick={run} disabled={!canExecute}>
              <span aria-hidden="true">▶</span> Run
            </button>
          )}
        </div>
      </section>

      {notice && (
        <div className="notice" role="alert">
          <span>!</span>
          <p>{notice}</p>
          <button onClick={() => setNotice(null)} aria-label="Dismiss message">×</button>
        </div>
      )}

      <div className="dashboard-grid">
        <section className="panel registers-panel">
          <div className="panel-heading">
            <div>
              <span className="eyebrow">Architectural state</span>
              <h2>Registers</h2>
            </div>
            <div className="legend"><span /> changed on last step</div>
          </div>
          <div className="register-grid" aria-label="RV32 integer registers">
            {(snapshot?.registers ?? []).map((register) => (
              <div className={`register ${register.changed ? "changed" : ""}`} key={register.index}>
                <div className="register-name">
                  <span>x{register.index.toString().padStart(2, "0")}</span>
                  <b>{register.abi}</b>
                </div>
                <code>{register.hex}</code>
              </div>
            ))}
            {!snapshot && Array.from({ length: 32 }, (_, index) => (
              <div className="register register-loading" key={index}>
                <div className="register-name"><span>x{index.toString().padStart(2, "0")}</span></div>
                <code>0x————————</code>
              </div>
            ))}
          </div>
        </section>

        <section className="panel performance-panel">
          <div className="panel-heading">
            <div>
              <span className="eyebrow">Observer feed</span>
              <h2>Performance model</h2>
            </div>
            <span className="cold-badge">COLD START</span>
          </div>
          <div className="metric-hero">
            <div>
              <span>Estimated cycles</span>
              <strong>{number(stats?.estimatedCycles)}</strong>
            </div>
            <div>
              <span>Configured CPI</span>
              <strong>{cpi}</strong>
            </div>
            <div>
              <span>Retired</span>
              <strong>{number(stats?.retired)}</strong>
            </div>
          </div>
          <div className="metric-stack">
            <MetricRow
              label="Instruction cache"
              detail={`${number(instructionCache?.misses)} misses / ${number(instructionCache?.accesses)} fetches`}
              value={percent(instructionCache?.misses ?? 0, instructionCache?.accesses ?? 0)}
              ratio={instructionCache?.accesses ? instructionCache.misses / instructionCache.accesses : 0}
              tone="amber"
            />
            <MetricRow
              label="Data cache"
              detail={`${number(dataCache?.misses)} misses / ${number(dataCache?.accesses)} accesses`}
              value={percent(dataCache?.misses ?? 0, dataCache?.accesses ?? 0)}
              ratio={dataCache?.accesses ? dataCache.misses / dataCache.accesses : 0}
              tone="blue"
            />
            <MetricRow
              label="Branch predictor"
              detail={`${number(branches?.correct)} correct / ${number(branches?.predictions)} predictions`}
              value={percent(branches?.correct ?? 0, branches?.predictions ?? 0)}
              ratio={branches?.predictions ? branches.correct / branches.predictions : 0}
              tone="green"
            />
          </div>
          <div className="model-footnote">
            <span>4 KiB split caches · 64 B lines · 2-way</span>
            <span>256-entry 2-bit predictor</span>
          </div>
        </section>

        <section className="panel memory-panel">
          <div className="panel-heading memory-heading">
            <div>
              <span className="eyebrow">Mapped guest bytes</span>
              <h2>Memory inspector</h2>
            </div>
            <div className="address-form">
              <input
                value={inspectAddress}
                onChange={(event) => setInspectAddress(event.target.value)}
                onKeyDown={(event) => event.key === "Enter" && inspect()}
                aria-label="Memory address"
                spellCheck={false}
              />
              <button onClick={() => inspect()}>Inspect</button>
            </div>
          </div>
          <div className="memory-shortcuts">
            <button onClick={() => snapshot && inspect(snapshot.pcHex)}>PC</button>
            <button onClick={() => snapshot?.registers[2] && inspect(snapshot.registers[2].hex)}>SP</button>
            <button onClick={() => snapshot?.load && inspect(snapshot.load.rangeBegin)}>Image base</button>
          </div>
          <div className="memory-table" role="table" aria-label="Hexadecimal memory view">
            <div className="memory-row memory-header" role="row">
              <span>ADDRESS</span>
              <span>00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F</span>
              <span>ASCII</span>
            </div>
            {memoryRows.map((row) => (
              <div className="memory-row" role="row" key={row.address}>
                <code>{row.address}</code>
                <code>{row.bytes.map((byte) => byte.toString(16).padStart(2, "0")).join(" ")}</code>
                <code>{row.ascii}</code>
              </div>
            ))}
            {!memoryRows.length && <div className="memory-empty">Memory appears after the ELF is mapped.</div>}
          </div>
        </section>

        <section className="panel console-panel">
          <div className="panel-heading console-heading">
            <div>
              <span className="eyebrow">Hosted Linux calls</span>
              <h2>Guest console</h2>
            </div>
            <div className="console-tabs" role="tablist" aria-label="Console stream">
              {(["all", "stdout", "stderr"] as const).map((mode) => (
                <button
                  key={mode}
                  className={consoleMode === mode ? "active" : ""}
                  onClick={() => setConsoleMode(mode)}
                  role="tab"
                  aria-selected={consoleMode === mode}
                >
                  {mode}
                </button>
              ))}
            </div>
          </div>
          <pre className="terminal-output">
            <span className="terminal-prompt">guest$ </span>
            {consoleText || <span className="terminal-muted">output will appear here</span>}
            <span className="terminal-cursor" aria-hidden="true" />
          </pre>
          <div className="console-status">
            <span>stdout {stdout.length} B</span>
            <span>stderr {stderr.length} B</span>
            <span>{number(stats?.environmentCalls)} hosted calls</span>
          </div>
        </section>

        <section className="panel event-panel">
          <div className="panel-heading">
            <div>
              <span className="eyebrow">Most recent boundary</span>
              <h2>Last commit</h2>
            </div>
            {snapshot?.lastEvent && <span className="event-kind">{snapshot.lastEvent.kind}</span>}
          </div>
          {snapshot?.lastEvent ? (
            <div className="event-body">
              <div className="event-assembly">
                <code>{snapshot.lastEvent.pc}</code>
                <strong>{snapshot.lastEvent.assembly ?? snapshot.lastEvent.cause ?? `syscall ${snapshot.lastEvent.number}`}</strong>
              </div>
              <div className="event-facts">
                {snapshot.lastEvent.instructionCache && (
                  <Fact label="I-cache" value={snapshot.lastEvent.instructionCache} alert={snapshot.lastEvent.instructionCache === "miss"} />
                )}
                {snapshot.lastEvent.dataCache && (
                  <Fact label="D-cache" value={snapshot.lastEvent.dataCache} alert={snapshot.lastEvent.dataCache === "miss"} />
                )}
                {snapshot.lastEvent.branchPrediction && (
                  <Fact label="Prediction" value={snapshot.lastEvent.branchPrediction} alert={snapshot.lastEvent.branchPrediction === "miss"} />
                )}
                {snapshot.lastEvent.nextPc && <Fact label="Next PC" value={snapshot.lastEvent.nextPc} />}
                {snapshot.lastEvent.value && <Fact label="Trap value" value={snapshot.lastEvent.value} alert />}
              </div>
            </div>
          ) : (
            <div className="event-empty">Step once to see the core’s post-commit observation.</div>
          )}
        </section>

        <section className="panel image-panel">
          <div className="panel-heading">
            <div>
              <span className="eyebrow">Loader + debugger</span>
              <h2>Image & breakpoints</h2>
            </div>
            <span className="file-type">ELF32</span>
          </div>
          <div className="file-card">
            <div className="file-icon">E</div>
            <div>
              <strong>{fileName}</strong>
              <span>{snapshot?.load ? `${number(snapshot.load.fileBytes)} file bytes · ${snapshot.load.segments} PT_LOAD` : "Validating image…"}</span>
            </div>
          </div>
          <div className="elf-grid">
            <Info label="Entry" value={snapshot?.load?.entryPoint ?? "—"} />
            <Info label="Image range" value={snapshot?.load ? `${snapshot.load.rangeBegin}–${snapshot.load.rangeEnd}` : "—"} />
            <Info label="Initial SP" value={snapshot?.load?.stackPointer ?? "—"} />
            <Info label="Guest map" value={snapshot?.load ? `${snapshot.load.memoryBase} / 16 MiB` : "—"} />
          </div>
          <div className="breakpoint-block">
            <div className="breakpoint-title">
              <span>Host breakpoints</span>
              <small>Checked before execution</small>
            </div>
            <div className="breakpoint-form">
              <input
                value={breakpointAddress}
                onChange={(event) => setBreakpointAddress(event.target.value)}
                onKeyDown={(event) => event.key === "Enter" && toggleBreakpoint(breakpointAddress, true)}
                aria-label="Breakpoint address"
                spellCheck={false}
              />
              <button onClick={() => toggleBreakpoint(breakpointAddress, true)}>Add</button>
            </div>
            <div className="breakpoint-list">
              {snapshot?.breakpoints.map((address) => (
                <button key={address} onClick={() => toggleBreakpoint(address, false)} title="Remove breakpoint">
                  <span>●</span>{address}<b>×</b>
                </button>
              ))}
              {!snapshot?.breakpoints.length && <span className="no-breakpoints">No breakpoints set</span>}
            </div>
          </div>
        </section>
      </div>

      <footer>
        <span>RVEMU 0.1 · C++20 core compiled to WebAssembly</span>
        <span>Step: F10 · Run/Pause: ⌘/Ctrl + Enter · Pause: Esc</span>
      </footer>
    </main>
  );
}

function MetricRow({
  label,
  detail,
  value,
  ratio,
  tone,
}: {
  label: string;
  detail: string;
  value: string;
  ratio: number;
  tone: "amber" | "blue" | "green";
}) {
  return (
    <div className="metric-row">
      <div className="metric-copy">
        <strong>{label}</strong>
        <span>{detail}</span>
      </div>
      <div className="metric-value">{value}</div>
      <div className="meter"><span className={`meter-${tone}`} style={{ width: `${Math.max(2, Math.min(100, ratio * 100))}%` }} /></div>
    </div>
  );
}

function Fact({ label, value, alert = false }: { label: string; value: string; alert?: boolean }) {
  return (
    <div className={`fact ${alert ? "fact-alert" : ""}`}>
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  );
}

function Info({ label, value }: { label: string; value: string }) {
  return (
    <div className="info-pair">
      <span>{label}</span>
      <code>{value}</code>
    </div>
  );
}
