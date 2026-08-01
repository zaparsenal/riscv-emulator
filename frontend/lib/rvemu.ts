export interface RegisterSnapshot {
  index: number;
  abi: string;
  value: number;
  hex: string;
  changed: boolean;
}

export interface InstructionSnapshot {
  address: number;
  addressHex: string;
  raw: number;
  rawHex: string;
  mnemonic: string;
  operands: string;
  assembly: string;
}

export interface CacheSnapshot {
  accesses: number;
  hits: number;
  misses: number;
}

export interface MachineSnapshot {
  ok: true;
  ready: boolean;
  status:
    | "empty"
    | "ready"
    | "paused"
    | "breakpoint"
    | "exited"
    | "trapped"
    | "error";
  message: string;
  pc: number;
  pcHex: string;
  instruction: InstructionSnapshot | null;
  registers: RegisterSnapshot[];
  load: null | {
    entryPoint: string;
    segments: number;
    fileBytes: number;
    zeroFillBytes: number;
    rangeBegin: string;
    rangeEnd: string;
    stackPointer: string;
    memoryBase: string;
    memoryBytes: number;
  };
  statistics: {
    guestSteps: number;
    retired: number;
    environmentCalls: number;
    instructionCache: CacheSnapshot;
    dataCache: CacheSnapshot;
    branches: {
      predictions: number;
      correct: number;
      incorrect: number;
      predictedTaken?: number;
      actualTaken?: number;
    };
    estimatedCycles: number | null;
  };
  stdout: string;
  stderr: string;
  breakpoints: string[];
  lastEvent: null | {
    kind: "instruction" | "syscall" | "exit" | "trap";
    pc: string;
    nextPc?: string;
    mnemonic?: string;
    assembly?: string;
    instructionCache?: "hit" | "miss";
    dataCache?: "hit" | "miss" | null;
    dataAddress?: string | null;
    branchTaken?: boolean | null;
    branchPrediction?: "correct" | "miss" | null;
    predictedTaken?: boolean | null;
    number?: number;
    result?: number;
    cause?: string;
    value?: string;
  };
}

export interface MemorySnapshot {
  ok: true;
  address: string;
  bytes: number[];
}

interface WasmModule {
  HEAPU8: Uint8Array;
  UTF8ToString(pointer: number): string;
  _rvemu_initialize(): number;
  _rvemu_allocate(size: number): number;
  _rvemu_deallocate(pointer: number): void;
  _rvemu_load_elf(pointer: number, size: number): number;
  _rvemu_reset(): number;
  _rvemu_step(): number;
  _rvemu_run_batch(maximumSteps: number): number;
  _rvemu_set_breakpoint(address: number, enabled: number): number;
  _rvemu_read_memory(address: number, size: number): number;
  _rvemu_snapshot(): number;
}

interface Failure {
  ok: false;
  error: string;
}

function assertSuccess<T extends { ok: true }>(value: T | Failure): T {
  if (!value.ok) {
    throw new Error(value.error);
  }
  return value;
}

export class RvemuBrowser {
  private constructor(private readonly wasm: WasmModule) {}

  static async create(): Promise<RvemuBrowser> {
    const moduleUrl = new URL("/wasm/rvemu.js", window.location.origin).href;
    const imported = await import(/* @vite-ignore */ moduleUrl);
    const wasm = (await imported.default({
      locateFile(path: string) {
        return new URL(`/wasm/${path}`, window.location.origin).href;
      },
    })) as WasmModule;
    const emulator = new RvemuBrowser(wasm);
    emulator.parse<MachineSnapshot | Failure>(wasm._rvemu_initialize());
    return emulator;
  }

  loadElf(bytes: Uint8Array): MachineSnapshot {
    const pointer = this.wasm._rvemu_allocate(bytes.byteLength);
    if (pointer === 0) {
      throw new Error("The browser could not allocate space for this ELF.");
    }
    try {
      this.wasm.HEAPU8.set(bytes, pointer);
      return assertSuccess(
        this.parse<MachineSnapshot | Failure>(
          this.wasm._rvemu_load_elf(pointer, bytes.byteLength),
        ),
      );
    } finally {
      this.wasm._rvemu_deallocate(pointer);
    }
  }

  snapshot(): MachineSnapshot {
    return assertSuccess(
      this.parse<MachineSnapshot | Failure>(this.wasm._rvemu_snapshot()),
    );
  }

  reset(): MachineSnapshot {
    return assertSuccess(
      this.parse<MachineSnapshot | Failure>(this.wasm._rvemu_reset()),
    );
  }

  step(): MachineSnapshot {
    return assertSuccess(
      this.parse<MachineSnapshot | Failure>(this.wasm._rvemu_step()),
    );
  }

  runBatch(maximumSteps = 500): MachineSnapshot {
    return assertSuccess(
      this.parse<MachineSnapshot | Failure>(
        this.wasm._rvemu_run_batch(maximumSteps),
      ),
    );
  }

  setBreakpoint(address: number, enabled: boolean): MachineSnapshot {
    return assertSuccess(
      this.parse<MachineSnapshot | Failure>(
        this.wasm._rvemu_set_breakpoint(address >>> 0, enabled ? 1 : 0),
      ),
    );
  }

  readMemory(address: number, size = 64): MemorySnapshot {
    return assertSuccess(
      this.parse<MemorySnapshot | Failure>(
        this.wasm._rvemu_read_memory(address >>> 0, size),
      ),
    );
  }

  private parse<T>(pointer: number): T {
    return JSON.parse(this.wasm.UTF8ToString(pointer)) as T;
  }
}
