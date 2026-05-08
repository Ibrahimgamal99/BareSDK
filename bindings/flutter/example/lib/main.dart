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
  late final BareSDK _sdk;
  late final Account _account;

  String _status   = 'Initializing...';
  Call?  _call;

  @override
  void initState() {
    super.initState();
    _sdk = BareSDK(logLevel: 1, statsIntervalMs: 5000);
    _account = _sdk.createAccount(
      'alice@pbx.example.com',
      'secret',
      transport: baresdk_transport_t.BARESDK_TRANSPORT_TLS,
    );

    _account.events.listen((ev) {
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
                     ev.state == baresdk_call_state_t.BARESDK_CALL_FAILED;
        setState(() {
          _status = done ? 'Call ended' : 'Call state: ${ev.state}';
          if (done) _call = null;
        });
      } else if (ev is MediaStatsEvent) {
        setState(() => _status = 'MOS: ${ev.mosLq.toStringAsFixed(2)}  RTT: ${ev.rttMs.toStringAsFixed(0)} ms');
      }
    });

    _account.register();
  }

  @override
  void dispose() {
    _account.destroy();
    _sdk.shutdown();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('BareSDK Demo')),
      body: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Text(_status, style: Theme.of(context).textTheme.headlineSmall),
            const SizedBox(height: 32),
            if (_call == null)
              ElevatedButton(
                onPressed: () {
                  final call = _account.call('bob@pbx.example.com');
                  setState(() => _call = call);
                },
                child: const Text('Call Bob'),
              )
            else
              Row(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  ElevatedButton(
                    onPressed: () { _call!.answer(); },
                    child: const Text('Answer'),
                  ),
                  const SizedBox(width: 16),
                  ElevatedButton(
                    style: ElevatedButton.styleFrom(backgroundColor: Colors.red),
                    onPressed: () { _call!.hangup(); },
                    child: const Text('Hang Up'),
                  ),
                ],
              ),
          ],
        ),
      ),
    );
  }
}
