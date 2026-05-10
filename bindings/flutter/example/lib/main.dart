import 'package:flutter/material.dart';
import 'package:baresdk/baresdk.dart';

void main() => runApp(const PhoneApp());

class PhoneApp extends StatelessWidget {
  const PhoneApp({super.key});
  @override
  Widget build(BuildContext context) {
    return const MaterialApp(title: 'BareSDK Demo', home: PhonePage());
  }
}

class PhonePage extends StatefulWidget {
  const PhonePage({super.key});
  @override
  State<PhonePage> createState() => _PhonePageState();
}

class _PhonePageState extends State<PhonePage> {
  BareSDK? _sdk;
  Account? _account;

  final _uriCtrl    = TextEditingController();
  final _passCtrl   = TextEditingController();
  final _calleeCtrl = TextEditingController();

  baresdk_transport_t _transport = baresdk_transport_t.BARESDK_TRANSPORT_UDP;

  String _status  = '';
  String _stats   = '';
  Call?  _call;
  bool   _muted   = false;
  bool   _mutedRx = false;

  void _register() {
    final uri  = _uriCtrl.text.trim();
    final pass = _passCtrl.text.trim();
    if (uri.isEmpty || pass.isEmpty) return;

    final sdk     = BareSDK(logLevel: 1, statsIntervalMs: 5000);
    final account = sdk.createAccount(uri, pass, transport: _transport);

    account.events.listen((ev) {
      if (ev is RegStateEvent) {
        setState(() {
          _status = ev.state == baresdk_reg_state_t.BARESDK_REG_REGISTERED
              ? 'Registered'
              : 'Registration state: ${ev.state}';
        });
      } else if (ev is IncomingCallEvent) {
        setState(() {
          _status = 'Incoming call from ${ev.fromUri}';
          _call   = ev.call;
        });
      } else if (ev is CallStateEvent) {
        final done = ev.state == baresdk_call_state_t.BARESDK_CALL_ENDED ||
                     ev.state == baresdk_call_state_t.BARESDK_CALL_FAILED ||
                     ev.state == baresdk_call_state_t.BARESDK_CALL_CANCELLED;
        setState(() {
          _status = done ? 'Call ended' : 'Call state: ${ev.state}';
          if (done) { _call = null; _stats = ''; _muted = false; _mutedRx = false; }
        });
      } else if (ev is MediaStatsEvent) {
        setState(() => _stats = _formatStats(ev));
      }
    });

    account.register();

    setState(() {
      _sdk     = sdk;
      _account = account;
      _status  = 'Registering…';
    });
  }

  void _logout() {
    _account?.destroy();
    _sdk?.shutdown();
    setState(() {
      _sdk     = null;
      _account = null;
      _status  = '';
      _stats   = '';
      _call    = null;
      _muted   = false;
      _mutedRx = false;
    });
  }

  String _formatStats(MediaStatsEvent s) {
    final method = s.mosMethod == 0 ? 'E-model' : 'simplified';
    return 'MOS-LQ ${s.mosLq.toStringAsFixed(2)} / CQ ${s.mosCq.toStringAsFixed(2)} ($method)\n'
        'RTT ${s.rttMs.toStringAsFixed(0)} ms  '
        'jitter ${s.jitterMs.toStringAsFixed(1)} ms\n'
        'loss TX ${s.lossPct.toStringAsFixed(1)}%  '
        'RX ${s.lossPctRx.toStringAsFixed(1)}%\n'
        'bw TX ${s.bandwidthTx} kbps  RX ${s.bandwidthRx} kbps\n'
        '(avg TX ${s.avgBandwidthTx}  RX ${s.avgBandwidthRx})\n'
        'jitter buf ${s.jitterBufferMs} ms  late ${s.latePackets}  discarded ${s.discardedPackets}\n'
        'codec ${s.codec} ${s.codecClockRate ~/ 1000} kHz  PT ${s.payloadType}\n'
        'pkts TX ${s.packetsSent}  RX ${s.packetsReceived}  '
        'lost TX ${s.packetsLost}  RX ${s.packetsLostRx}\n'
        '${s.remoteAddr}  SSRC rx ${s.ssrcRx}';
  }

  @override
  void dispose() {
    _uriCtrl.dispose();
    _passCtrl.dispose();
    _calleeCtrl.dispose();
    _account?.destroy();
    _sdk?.shutdown();
    super.dispose();
  }

  void _dial() {
    final uri = _calleeCtrl.text.trim();
    if (uri.isEmpty) return;
    final callee = uri.startsWith('sip:') ? uri : 'sip:$uri';
    final call = _account!.call(callee);
    setState(() => _call = call);
  }

  void _toggleMute() {
    _muted = !_muted;
    _call?.mute(on: _muted);
    setState(() {});
  }

  void _toggleMuteRx() {
    _mutedRx = !_mutedRx;
    _call?.muteRx(on: _mutedRx);
    setState(() {});
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('BareSDK Demo'),
        actions: [
          if (_account != null)
            IconButton(
              icon: const Icon(Icons.logout),
              tooltip: 'Logout',
              onPressed: _logout,
            ),
        ],
      ),
      body: _account == null ? _buildLoginForm() : _buildPhone(),
    );
  }

  Widget _buildLoginForm() {
    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          TextField(
            controller: _uriCtrl,
            decoration: const InputDecoration(
              labelText: 'SIP URI',
              hintText: 'alice@pbx.example.com',
              border: OutlineInputBorder(),
            ),
            keyboardType: TextInputType.emailAddress,
          ),
          const SizedBox(height: 12),
          TextField(
            controller: _passCtrl,
            decoration: const InputDecoration(
              labelText: 'Password',
              border: OutlineInputBorder(),
            ),
            obscureText: true,
            onSubmitted: (_) => _register(),
          ),
          const SizedBox(height: 12),
          DropdownButtonFormField<baresdk_transport_t>(
            value: _transport,
            decoration: const InputDecoration(
              labelText: 'Transport',
              border: OutlineInputBorder(),
            ),
            items: const [
              DropdownMenuItem(
                value: baresdk_transport_t.BARESDK_TRANSPORT_UDP,
                child: Text('UDP'),
              ),
              DropdownMenuItem(
                value: baresdk_transport_t.BARESDK_TRANSPORT_TCP,
                child: Text('TCP'),
              ),
              DropdownMenuItem(
                value: baresdk_transport_t.BARESDK_TRANSPORT_TLS,
                child: Text('TLS'),
              ),
            ],
            onChanged: (v) => setState(() => _transport = v!),
          ),
          const SizedBox(height: 16),
          ElevatedButton.icon(
            icon: const Icon(Icons.login),
            label: const Text('Register'),
            onPressed: _register,
          ),
        ],
      ),
    );
  }

  Widget _buildPhone() {
    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.center,
        children: [
          Text(_status, style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 16),

          if (_stats.isNotEmpty)
            Container(
              width: double.infinity,
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.black87,
                borderRadius: BorderRadius.circular(8),
              ),
              child: Text(
                _stats,
                style: const TextStyle(
                  color: Colors.greenAccent,
                  fontFamily: 'monospace',
                  fontSize: 12,
                ),
              ),
            ),

          const SizedBox(height: 24),

          if (_call == null) ...[
            TextField(
              controller: _calleeCtrl,
              decoration: const InputDecoration(
                labelText: 'Callee URI',
                hintText: 'bob@pbx.example.com',
                border: OutlineInputBorder(),
              ),
              keyboardType: TextInputType.url,
              onSubmitted: (_) => _dial(),
            ),
            const SizedBox(height: 12),
            ElevatedButton.icon(
              icon: const Icon(Icons.call),
              label: const Text('Dial'),
              onPressed: _dial,
            ),
          ] else
            Column(
              children: [
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    ElevatedButton(
                      onPressed: () { _call!.answer(); },
                      child: const Text('Answer'),
                    ),
                    const SizedBox(width: 12),
                    ElevatedButton(
                      style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
                      onPressed: () { _call!.hangup(); },
                      child: const Text('Hang Up'),
                    ),
                  ],
                ),
                const SizedBox(height: 12),
                Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    ElevatedButton.icon(
                      icon: Icon(_muted ? Icons.mic_off : Icons.mic),
                      label: Text(_muted ? 'Unmute' : 'Mute'),
                      onPressed: _toggleMute,
                    ),
                    const SizedBox(width: 12),
                    ElevatedButton.icon(
                      icon: Icon(_mutedRx ? Icons.volume_off : Icons.volume_up),
                      label: Text(_mutedRx ? 'Unmute Speaker' : 'Mute Speaker'),
                      onPressed: _toggleMuteRx,
                    ),
                  ],
                ),
              ],
            ),
        ],
      ),
    );
  }
}
