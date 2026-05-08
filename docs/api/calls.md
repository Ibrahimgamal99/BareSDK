# Calls

## Outgoing call

```c
baresdk_call_handle_t call;
int rc = baresdk_call_invite(acct, "sip:bob@pbx.example.com", &call);
// fires: CALLING → RINGING → ESTABLISHED  (or FAILED)
```

## Incoming call

```c
// In your event callback:
case BARESDK_EV_INCOMING_CALL:
    printf("Incoming from %s\n", ev->u.incoming.from_uri);
    baresdk_call_answer(ev->u.incoming.call);
    break;
```

To reject without answering, call `baresdk_call_hangup(call)` before answering.

## Hang up / terminate

```c
baresdk_call_hangup(call);   // sends BYE; fires BARESDK_EV_CALL_STATE (ENDED)
```

## Hold and resume

```c
baresdk_call_hold(call);     // re-INVITE with sendonly
baresdk_call_resume(call);   // re-INVITE with sendrecv
```

## DTMF

```c
baresdk_call_send_dtmf(call, '5');   // RFC 4733 RTP event
// valid: '0'-'9', '*', '#', 'A'-'D'
```

## Transfer

### Blind transfer (REFER)
```c
baresdk_call_transfer(call, "sip:carol@pbx.example.com");
```

### Attended transfer
```c
// call_a is the original call (to transfer away)
// call_b is the consultation call already established
baresdk_call_attended_transfer(call_a, call_b);
// embeds Replaces header from call_b's dialog
```

## Per-dialog custom headers

Headers attached to the **specific call's** subsequent re-INVITEs, BYE, and REFER:

```c
baresdk_call_add_header(call, "X-Call-Context", "helpdesk-123");
```

## Get current stats (synchronous)

```c
baresdk_ev_media_stats_t stats;
baresdk_call_get_stats(call, &stats);
printf("MOS-LQ: %.2f  loss: %.1f%%\n", stats.mos_lq, stats.loss_pct);
```

Stats are also delivered automatically via `BARESDK_EV_MEDIA_STATS` if `cfg.stats_interval_ms > 0`.
