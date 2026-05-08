/// <reference types="node" />
import { EventEmitter } from 'events';

export declare const Transport: {
  UDP: 0;
  TCP: 1;
  TLS: 2;
  WS:  3;
  WSS: 4;
};

export interface AccountConfig {
  uri:         string;
  password:    string;
  transport?:  0 | 1 | 2 | 3 | 4;
  serverHost?: string;
  serverUrl?:  string;
}

export interface SDKOptions {
  logLevel?:      number;
  statsInterval?: number;
  traceSip?:      boolean;
}

export declare class Call {
  answer(): void;
  hangup(): void;
  hold(): void;
  resume(): void;
  sendDtmf(digit: string): void;
  transfer(uri: string): void;
  mute(on?: boolean): void;
}

export declare class Account {
  register(): number;
  unregister(): number;
  call(uri: string): Call;
  sendMessage(to: string, body: string, contentType?: string): void;
}

export declare interface SDKEvents {
  registered:         (ev: { type: 'reg_state'; state: number }) => void;
  registrationFailed: (ev: { type: 'reg_state'; state: number; error: number; errorStr: string | null }) => void;
  incomingCall:       (ev: { type: 'incoming_call'; callHandle: number; fromUri: string; displayName: string | null }) => void;
  callState:          (ev: { type: 'call_state'; callHandle: number; state: number; reason: string | null }) => void;
  dtmf:               (ev: { type: 'dtmf'; callHandle: number; digit: string }) => void;
  mediaStats:         (ev: { type: 'media_stats'; callHandle: number; mosLq: number; mosCq: number; rttMs: number; lossPct: number; bandwidthTx: number; bandwidthRx: number; codec: string }) => void;
  sipTrace:           (ev: { type: 'sip_trace'; dir: number; transport: string; remoteAddr: string; rawMessage: string }) => void;
  message:            (ev: { type: 'message'; fromUri: string; body: string; contentType: string }) => void;
  log:                (ev: { type: 'log'; message: string }) => void;
}

export declare class SDK extends EventEmitter {
  constructor(opts?: SDKOptions);
  createAccount(config: AccountConfig): Account;
  get version(): string;
  shutdown(): void;

  on<K extends keyof SDKEvents>(event: K, listener: SDKEvents[K]): this;
  emit<K extends keyof SDKEvents>(event: K, ...args: Parameters<SDKEvents[K]>): boolean;
}
