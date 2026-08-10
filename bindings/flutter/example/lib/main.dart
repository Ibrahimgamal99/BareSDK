/// baresdk example softphone.
///
/// Three tabs:
///  1. Account  — registration over UDP/TCP/TLS/WS/WSS, codec preference,
///                retry controls.
///  2. Call     — dial/answer/hold/mute/transfer, DTMF pad, speakerphone.
///  3. Diagnostics — live media stats, quality alerts, handover timeline,
///                in-app log console (the SDK never writes to the terminal).
library;

import 'dart:async';

import 'package:baresdk/baresdk.dart';
import 'package:flutter/material.dart';
import 'package:permission_handler/permission_handler.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(const BareSDKExampleApp());
}

class BareSDKExampleApp extends StatelessWidget {
  const BareSDKExampleApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'baresdk example',
      theme: ThemeData(colorSchemeSeed: Colors.teal, useMaterial3: true),
      home: const PhonePage(),
    );
  }
}

class PhonePage extends StatefulWidget {
  const PhonePage({super.key});

  @override
  State<PhonePage> createState() => _PhonePageState();
}

class _PhonePageState extends State<PhonePage>
    with SingleTickerProviderStateMixin {
  late final TabController _tabs = TabController(length: 3, vsync: this);

  BareSDK? _sdk;
  Account? _account;
  Call? _call;
  StreamSubscription<BareSDKEvent>? _sub;

  // ── form state ─────────────────────────────────────────────────────────
  final _user = TextEditingController(text: 'alice@pbx.example.com');
  final _pass = TextEditingController();
  final _serverUrl = TextEditingController();
  final _callee = TextEditingController();
  final _transferTo = TextEditingController();
  Transport _transport = Transport.udp;
  MediaEncryption _mediaEnc = MediaEncryption.none;
  bool _ice = false;
  bool _verifyTls = true;
  final List<String> _codecPrefs = ['opus', 'g722', 'ulaw', 'alaw'];
  final Set<String> _enabledCodecs = {'opus', 'ulaw'};

  // ── live state ─────────────────────────────────────────────────────────
  RegState _regState = RegState.unregistered;
  String _regDetail = '';
  CallState? _callState;
  String _callPeer = '';
  bool _muted = false;
  bool _held = false;
  bool _speaker = false;
  MediaStats? _stats;
  final List<String> _alerts = [];
  final List<String> _handover = [];
  final List<String> _log = [];

  @override
  void dispose() {
    _sub?.cancel();
    _sdk?.shutdown();
    _tabs.dispose();
    super.dispose();
  }

  void _snack(String msg) {
    if (!mounted) return;
    ScaffoldMessenger.of(context)
        .showSnackBar(SnackBar(content: Text(msg)));
  }

  void _logLine(String s) {
    setState(() {
      _log.add(s.trimRight());
      if (_log.length > 500) _log.removeRange(0, _log.length - 500);
    });
  }

  // ── registration ───────────────────────────────────────────────────────

  Future<void> _register() async {
    await Permission.microphone.request();

    try {
      _sdk ??= await BareSDK.start(
        config: const BareSDKConfig(
          logLevel: 2,
          statsIntervalMs: 2000,
          traceSdpDiff: true,
          mosAlertThreshold: 3.5,
          lossAlertThreshold: 5.0,
          jitterAlertThreshold: 40.0,
        ),
      );
    } on StateError catch (e) {
      _snack(e.message);
      return;
    }

    final needsUrl =
        _transport == Transport.ws || _transport == Transport.wss;
    if (needsUrl && _serverUrl.text.isEmpty) {
      _snack('WS/WSS needs a server URL, e.g. wss://pbx:8089/ws');
      return;
    }

    final account = _sdk!.createAccount(
      _user.text,
      _pass.text,
      config: AccountConfig(
        transport: _transport,
        serverUrl: _serverUrl.text.isEmpty ? null : _serverUrl.text,
        mediaEnc: _mediaEnc,
        iceEnabled: _ice,
        verifyTls: _verifyTls,
        audioCodecs:
            _codecPrefs.where(_enabledCodecs.contains).toList(),
      ),
    );
    _account = account;

    _sub?.cancel();
    _sub = _sdk!.events.listen(_onEvent);

    account.register();
    setState(() => _regState = RegState.registering);
  }

  void _unregister() {
    _account?.destroy();
    _account = null;
    setState(() {
      _regState = RegState.unregistered;
      _regDetail = '';
      _call = null;
      _callState = null;
    });
  }

  // ── event handling ─────────────────────────────────────────────────────

  void _onEvent(BareSDKEvent ev) {
    if (!mounted) return;
    switch (ev) {
      case RegStateEvent e:
        setState(() {
          _regState = e.state;
          _regDetail = switch (e.state) {
            RegState.failed =>
              '${e.errorStr ?? e.error.name} — retry #${e.retryAttempt} '
                  'in ${(e.retryDelayMs / 1000).toStringAsFixed(0)}s',
            _ => '',
          };
        });

      case IncomingCallEvent e:
        setState(() {
          _call = e.call;
          _callState = CallState.ringing;
          _callPeer = e.displayName ?? e.fromUri;
        });
        _tabs.animateTo(1);
        _snack('Incoming call from $_callPeer');

      case CallStateEvent e:
        setState(() {
          _callState = e.state;
          if (e.state.isTerminal) {
            _call = null;
            _stats = null;
            _muted = false;
            _held = false;
            if (e.reason != null) _logLine('call ended: ${e.reason}');
          }
        });

      case CallDtmfEvent e:
        _logLine('DTMF received: ${e.digit}');

      case MediaStatsEvent e:
        setState(() => _stats = e.stats);

      case QualityAlertEvent e:
        final msg = e.recovering
            ? '${e.issue.name} recovered '
                '(${e.value.toStringAsFixed(1)})'
            : '${e.issue.name} ${e.value.toStringAsFixed(1)} crossed '
                '${e.threshold.toStringAsFixed(1)}';
        setState(() {
          _alerts.insert(0, '${_ts()} $msg');
          if (_alerts.length > 50) _alerts.removeLast();
        });
        if (!e.recovering) _snack('Quality alert: $msg');

      case NetworkEvent e:
        setState(() {
          _handover.insert(
              0,
              '${_ts()} ${e.stage.name}'
              '${e.attempt > 0 ? ' ${e.attempt}/${e.maxAttempts}' : ''}'
              '${e.localAddr.isNotEmpty ? ' @${e.localAddr}' : ''}'
              '${e.stage == NetworkStage.callMigrated ? ' (audio back after ${(e.elapsedMs / 1000).toStringAsFixed(1)}s)' : ''}');
          if (_handover.length > 50) _handover.removeLast();
        });

      case SdpNegotiationEvent e:
        _logLine('SDP: codec=${e.negotiatedCodec ?? '?'} '
            'crypto=${e.negotiatedCrypto ?? '?'}');

      case TransferRequestEvent e:
        _snack('Peer asks to transfer us to ${e.referToUri}');

      case RegistrarWarningEvent e:
        _logLine('registrar warning: ${e.message}');

      case MwiEvent e:
        _snack('Voicemail: ${e.newVoice} new / ${e.oldVoice} old');

      case MessageEvent e:
        _snack('${e.fromUri}: ${e.body}');

      case LogEvent e:
        _logLine(e.message);

      default:
        break;
    }
  }

  static String _ts() {
    final now = DateTime.now();
    return '${now.hour.toString().padLeft(2, '0')}:'
        '${now.minute.toString().padLeft(2, '0')}:'
        '${now.second.toString().padLeft(2, '0')}';
  }

  // ── call actions ───────────────────────────────────────────────────────

  void _dial() {
    final acct = _account;
    if (acct == null || _callee.text.isEmpty) return;
    setState(() {
      _call = acct.call(_callee.text);
      _callState = CallState.calling;
      _callPeer = _callee.text;
    });
  }

  // ── UI ─────────────────────────────────────────────────────────────────

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('baresdk'),
        bottom: TabBar(controller: _tabs, tabs: const [
          Tab(icon: Icon(Icons.account_circle), text: 'Account'),
          Tab(icon: Icon(Icons.call), text: 'Call'),
          Tab(icon: Icon(Icons.monitor_heart), text: 'Diagnostics'),
        ]),
      ),
      body: TabBarView(controller: _tabs, children: [
        _buildAccountTab(),
        _buildCallTab(),
        _buildDiagnosticsTab(),
      ]),
    );
  }

  Widget _statusBanner() {
    final (color, text) = switch (_regState) {
      RegState.registered => (Colors.green, 'Registered'),
      RegState.registering => (Colors.orange, 'Registering…'),
      RegState.failed => (Colors.red, 'Failed: $_regDetail'),
      RegState.unregistering => (Colors.orange, 'Unregistering…'),
      RegState.unregistered => (Colors.grey, 'Not registered'),
    };
    return Material(
      color: color.withOpacity(.15),
      child: Padding(
        padding: const EdgeInsets.all(12),
        child: Row(children: [
          Icon(Icons.circle, color: color, size: 12),
          const SizedBox(width: 8),
          Expanded(child: Text(text)),
          if (_regState == RegState.failed) ...[
            TextButton(
              onPressed: () => _account?.retryNow(),
              child: const Text('Retry now'),
            ),
            TextButton(
              onPressed: () => _account?.cancelRetry(),
              child: const Text('Stop'),
            ),
          ],
        ]),
      ),
    );
  }

  Widget _buildAccountTab() {
    final registered = _regState == RegState.registered ||
        _regState == RegState.registering;
    return ListView(padding: const EdgeInsets.all(16), children: [
      _statusBanner(),
      const SizedBox(height: 16),
      TextField(
        controller: _user,
        decoration: const InputDecoration(
            labelText: 'SIP URI', hintText: 'user@pbx.example.com'),
        enabled: !registered,
      ),
      TextField(
        controller: _pass,
        decoration: const InputDecoration(labelText: 'Password'),
        obscureText: true,
        enabled: !registered,
      ),
      DropdownButtonFormField<Transport>(
        value: _transport,
        decoration: const InputDecoration(labelText: 'Transport'),
        items: Transport.values
            .map((t) => DropdownMenuItem(
                value: t, child: Text(t.name.toUpperCase())))
            .toList(),
        onChanged: registered
            ? null
            : (t) => setState(() => _transport = t ?? Transport.udp),
      ),
      TextField(
        controller: _serverUrl,
        decoration: const InputDecoration(
          labelText: 'Server URL (required for WS/WSS)',
          hintText: 'wss://pbx.example.com:8089/ws',
        ),
        enabled: !registered,
      ),
      DropdownButtonFormField<MediaEncryption>(
        value: _mediaEnc,
        decoration: const InputDecoration(labelText: 'Media encryption'),
        items: const [
          DropdownMenuItem(
              value: MediaEncryption.none, child: Text('None (RTP)')),
          DropdownMenuItem(
              value: MediaEncryption.sdes, child: Text('SDES-SRTP')),
          DropdownMenuItem(
              value: MediaEncryption.dtlsSrtp, child: Text('DTLS-SRTP')),
        ],
        onChanged: registered
            ? null
            : (v) => setState(() => _mediaEnc = v ?? MediaEncryption.none),
      ),
      SwitchListTile(
        title: const Text('ICE'),
        value: _ice,
        onChanged: registered ? null : (v) => setState(() => _ice = v),
      ),
      SwitchListTile(
        title: const Text('Verify TLS certificate'),
        value: _verifyTls,
        onChanged:
            registered ? null : (v) => setState(() => _verifyTls = v),
      ),
      const SizedBox(height: 8),
      Text('Codecs (drag to reorder, tap to toggle)',
          style: Theme.of(context).textTheme.labelLarge),
      SizedBox(
        height: 72,
        child: ReorderableListView(
          scrollDirection: Axis.horizontal,
          onReorder: registered
              ? (a, b) {}
              : (oldIdx, newIdx) => setState(() {
                    if (newIdx > oldIdx) newIdx--;
                    _codecPrefs.insert(
                        newIdx, _codecPrefs.removeAt(oldIdx));
                  }),
          children: [
            for (final codec in _codecPrefs)
              Padding(
                key: ValueKey(codec),
                padding: const EdgeInsets.all(8),
                child: FilterChip(
                  label: Text(codec),
                  selected: _enabledCodecs.contains(codec),
                  onSelected: registered
                      ? null
                      : (on) => setState(() {
                            on
                                ? _enabledCodecs.add(codec)
                                : _enabledCodecs.remove(codec);
                          }),
                ),
              ),
          ],
        ),
      ),
      const SizedBox(height: 16),
      FilledButton.icon(
        icon: Icon(registered ? Icons.logout : Icons.login),
        label: Text(registered ? 'Unregister' : 'Register'),
        onPressed: registered ? _unregister : _register,
      ),
    ]);
  }

  Widget _buildCallTab() {
    final call = _call;
    final inCall = call != null;
    return ListView(padding: const EdgeInsets.all(16), children: [
      _statusBanner(),
      const SizedBox(height: 12),
      if (!inCall) ...[
        TextField(
          controller: _callee,
          decoration: const InputDecoration(
              labelText: 'Call to', hintText: 'bob@pbx.example.com'),
        ),
        const SizedBox(height: 12),
        FilledButton.icon(
          icon: const Icon(Icons.call),
          label: const Text('Dial'),
          onPressed: _regState == RegState.registered ? _dial : null,
        ),
      ] else ...[
        Card(
          child: ListTile(
            leading: const Icon(Icons.person),
            title: Text(_callPeer),
            subtitle: Text(_callState?.name ?? ''),
          ),
        ),
        const SizedBox(height: 8),
        Wrap(spacing: 8, runSpacing: 8, children: [
          if (_callState == CallState.ringing)
            FilledButton.icon(
              icon: const Icon(Icons.call),
              label: const Text('Answer'),
              onPressed: () => call.answer(),
            ),
          FilledButton.tonalIcon(
            icon: const Icon(Icons.call_end),
            label: const Text('Hang up'),
            onPressed: () => call.hangup(),
          ),
          FilledButton.tonalIcon(
            icon: Icon(_held ? Icons.play_arrow : Icons.pause),
            label: Text(_held ? 'Resume' : 'Hold'),
            onPressed: () {
              _held ? call.resume() : call.hold();
              setState(() => _held = !_held);
            },
          ),
          FilledButton.tonalIcon(
            icon: Icon(_muted ? Icons.mic : Icons.mic_off),
            label: Text(_muted ? 'Unmute' : 'Mute'),
            onPressed: () {
              call.mute(on: !_muted);
              setState(() => _muted = !_muted);
            },
          ),
          FilledButton.tonalIcon(
            icon:
                Icon(_speaker ? Icons.phone_in_talk : Icons.volume_up),
            label: Text(_speaker ? 'Earpiece' : 'Speaker'),
            onPressed: () {
              _sdk?.setSpeakerphone(!_speaker);
              setState(() => _speaker = !_speaker);
            },
          ),
        ]),
        const Divider(height: 32),
        Text('DTMF', style: Theme.of(context).textTheme.labelLarge),
        _dtmfPad(call),
        const Divider(height: 32),
        TextField(
          controller: _transferTo,
          decoration: const InputDecoration(
              labelText: 'Transfer to',
              hintText: 'carol@pbx.example.com'),
        ),
        const SizedBox(height: 8),
        OutlinedButton.icon(
          icon: const Icon(Icons.phone_forwarded),
          label: const Text('Blind transfer'),
          onPressed: () {
            if (_transferTo.text.isNotEmpty) call.transfer(_transferTo.text);
          },
        ),
      ],
    ]);
  }

  Widget _dtmfPad(Call call) {
    const keys = ['1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#'];
    return GridView.count(
      crossAxisCount: 3,
      shrinkWrap: true,
      physics: const NeverScrollableScrollPhysics(),
      childAspectRatio: 1.9,
      children: [
        for (final k in keys)
          Padding(
            padding: const EdgeInsets.all(4),
            child: OutlinedButton(
              onPressed: () {
                call.sendDtmf(k);
                _logLine('DTMF sent: $k');
              },
              child: Text(k, style: const TextStyle(fontSize: 20)),
            ),
          ),
      ],
    );
  }

  Widget _buildDiagnosticsTab() {
    final s = _stats;
    return ListView(padding: const EdgeInsets.all(16), children: [
      Text('Media stats', style: Theme.of(context).textTheme.titleMedium),
      Card(
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: s == null
              ? const Text('No active call / stats not started yet.')
              : Text(
                  'codec        ${s.codec} @${s.codecClockRate} Hz  pt=${s.payloadType}\n'
                  'MOS-LQ       ${s.mosLq.toStringAsFixed(2)}  (rx ${s.mosLqRx.toStringAsFixed(2)}, '
                  'min ${s.mosLqMin.toStringAsFixed(2)}, avg ${s.mosLqAvg.toStringAsFixed(2)})\n'
                  'RTT          ${s.rttMs.toStringAsFixed(1)} ms\n'
                  'jitter       rx ${s.jitterMs.toStringAsFixed(1)} ms / tx ${s.txJitterMs.toStringAsFixed(1)} ms\n'
                  'loss         tx ${s.lossPct.toStringAsFixed(1)}% / rx ${s.lossPctRx.toStringAsFixed(1)}%\n'
                  'bandwidth    up ${s.bandwidthTx} / down ${s.bandwidthRx} kbps '
                  '(avg ${s.avgBandwidthTx}/${s.avgBandwidthRx})\n'
                  'jitter buf   ${s.jitterBufferMs} ms, ${s.jitterBufferLoad} pkts, '
                  'late ${s.latePackets}, drop ${s.discardedPackets}\n'
                  'PLC          ${s.plcFrames} frames (${(s.plcRatio * 100).toStringAsFixed(1)}%)\n'
                  'level        spk ${s.audioLevelDbov.toStringAsFixed(0)} dBov / '
                  'mic ${s.micLevelDbov.toStringAsFixed(0)} dBov\n'
                  'packets      tx ${s.packetsSent} / rx ${s.packetsReceived}\n'
                  'remote       ${s.remoteAddr}  duration ${(s.callDurationMs / 1000).toStringAsFixed(0)}s',
                  style: const TextStyle(
                      fontFamily: 'monospace', fontSize: 12),
                ),
        ),
      ),
      const SizedBox(height: 12),
      Text('Quality alerts',
          style: Theme.of(context).textTheme.titleMedium),
      Card(
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: _alerts.isEmpty
              ? const Text('None.')
              : Text(_alerts.join('\n'),
                  style: const TextStyle(
                      fontFamily: 'monospace', fontSize: 12)),
        ),
      ),
      const SizedBox(height: 12),
      Text('Network handover',
          style: Theme.of(context).textTheme.titleMedium),
      Card(
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: _handover.isEmpty
              ? const Text('No network changes observed.')
              : Text(_handover.join('\n'),
                  style: const TextStyle(
                      fontFamily: 'monospace', fontSize: 12)),
        ),
      ),
      const SizedBox(height: 12),
      Row(children: [
        Text('SDK log', style: Theme.of(context).textTheme.titleMedium),
        const Spacer(),
        TextButton(
          onPressed: () => setState(_log.clear),
          child: const Text('Clear'),
        ),
      ]),
      Card(
        color: Colors.black,
        child: Padding(
          padding: const EdgeInsets.all(12),
          child: Text(
            _log.isEmpty ? '(empty)' : _log.reversed.take(100).join('\n'),
            style: const TextStyle(
                color: Colors.greenAccent,
                fontFamily: 'monospace',
                fontSize: 11),
          ),
        ),
      ),
    ]);
  }
}
