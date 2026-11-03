# RPW cluster head (ESP32)

Firmware for the NAKHEL cluster head. It receives Red Palm Weevil sensor data from the nodes over LoRa, buffers it, and delivers it to the cloud server over Wi-Fi.

```
nodes --LoRa--> src/lora_receiver.cpp --> buffer --> uplink with ARQ --HTTP(S)--> server
                YOU IMPLEMENT THIS        you may    DONE
                                          replace it
```

**The uplink is finished: it talks to the server, handles retries and sends health reports. What's left is the LoRa side**, which is the three TODOs in `src/lora_receiver.cpp`, plus a decision about the buffer.

Status: the uplink has been tested on a PC against the real server, and the ESP32 build compiles. **It hasn't run on a real ESP32 yet, so step 4 below is also the first test of its Wi-Fi and networking code.**

## 1. What you need

- A classic ESP32 dev board (`esp32dev`). Other chips, such as the C3, aren't supported as is: the firmware pins tasks to two cores.
- A USB data cable, and the board's USB driver if your computer doesn't detect it (usually CP210x or CH340).
- [VS Code](https://code.visualstudio.com/) with the [PlatformIO extension](https://platformio.org/install/ide?install=vscode). PlatformIO downloads the rest on the first build.
- A **2.4 GHz** Wi-Fi network with a plain password. Campus networks with a login page or per-user login won't work, but a phone hotspot will.
- From the person running the server: the **server URL** and this cluster head's **API key**. They create the key by registering the cluster head and its node IDs, and each node you test with must be registered.

## 2. Configure

```bash
git clone https://github.com/HaithamAyyash/rpw-clusterhead.git
cd rpw-clusterhead
cp include/secrets.example.h include/secrets.h
```

Fill in `include/secrets.h`. Git ignores this file, so **never commit the API key or paste it into the code**.

| Setting | Value |
|---|---|
| `NAKHEL_WIFI_SSID`, `NAKHEL_WIFI_PASSWORD` | Your Wi-Fi |
| `NAKHEL_SERVER_URL` | From the server admin, with no trailing slash |
| `NAKHEL_API_KEY` | From the server admin |
| `NAKHEL_ROOT_CA` | Leave empty for `http://`. Required for `https://`: the build fails without it |

## 3. Build and flash

In VS Code, use the PlatformIO toolbar, or from a terminal:

```bash
pio run -e esp32dev_demo -t upload -t monitor   # test build: sends fake data from node 1
pio run -e esp32dev -t upload -t monitor        # real firmware
```

| Build | Use it for |
|---|---|
| `esp32dev_demo` | Testing the uplink with no radio. Adds a fake measurement from node 1 every minute. **Never deploy this: it puts fake data in the real database** |
| `esp32dev` | The real firmware. It sends only cluster health until your receiver is filled in |

## 4. Check the uplink before writing any LoRa code

Flash `esp32dev_demo` and watch the serial monitor (115200 baud). Node 1 must be registered. Within about a minute you should see:

```
[demo] measurement queued, 1 waiting
[uplink] delivered node 1 at 2026-11-03T12:00:04Z (stored)
```

| Serial output | Meaning | Fix |
|---|---|---|
| Nothing from the demo | No Wi-Fi, or the clock isn't set | Check the Wi-Fi details and that it's 2.4 GHz. NTP needs outbound UDP port 123 |
| `[wifi] reconnecting` repeating | Can't join the network | Same as above |
| `send failed (status -1), retrying` | Can't reach the server | Check the URL, and try opening it from a phone on the same network |
| `send failed (status 200), retrying` | Something other than our server answered, such as a login page | Use another network |
| `rejected a record: unknown_node` | The node isn't registered under this key | Ask the admin to register it |
| `API key refused` | Bad or revoked key. Sending stops, but the buffer is kept | Put a valid key in `secrets.h` and reflash |
| `warning: clock is N s off server time` | The clock is wrong | Check NTP |

## 5. What you need to implement

All three TODOs are in `src/lora_receiver.cpp`. The skeleton already runs in its own task on core 1, builds the record and adds it to the buffer.

**TODO 1: `RawPacket`.** Replace it with your packet layout.

**TODO 2: `receivePacket()`.** Set up the radio, and return `true` once a complete packet has been copied in. Add your LoRa library under `lib_deps` in `platformio.ini`, in the `[esp32]` section.
- Check the packet (CRC, for example) before returning it, since the server can't tell whether a value is corrupted.
- An interrupt must only set a flag or notify the task. **`push()` isn't safe to call from an interrupt.**

**TODO 3: `convertMfcc()`.** Fill `out.mfcc[30][13]` with floats: 30 frames, 13 coefficients each.
- **To agree with the team:** the nodes send 15 × 13 int8 values plus a scale, and the server requires exactly 30 × 13. How to get from one to the other isn't defined yet, and it has to match what the model expects.
- NaN or infinity makes the whole record invalid: it is dropped without being sent.

### The timestamp rules

The server identifies a measurement by node ID and timestamp, so getting these wrong loses data or stores it twice.

- **Use the time the node recorded the sample**, in Unix seconds UTC, if the packet carries it. Otherwise use the arrival time, as the skeleton does.
- **Set it once, then never change it.** A record with a new timestamp counts as new data. This matters if your own buffer restores records after a reboot, so store the timestamp with the packet.
- **Each node needs a different timestamp per measurement.** Timestamps are whole seconds, and a second measurement with the same node and second is discarded as a duplicate.
- **Don't stamp anything before the clock is set.** A wrong time gets the record refused for good. The skeleton drops such packets; you could hold them until the clock is set instead.

### Node IDs

`nodeId` is one byte (0–255) and must be registered under this cluster head's API key. Whether any IDs are reserved, such as 255 for broadcast, is still to be confirmed.

### Node heartbeats (optional)

If your nodes report battery or uptime, queue them with `Kind::NodeHealth` and fill in `record.nodeHealth`. The dashboard shows "no data yet" until they do.

## 6. The buffer

The default buffer, `RamQueue` in `src/app_queue.h`, works as is. Its limits are the decision for you:

| Limit | What happens |
|---|---|
| Stored in RAM | **A reboot or power cut loses everything waiting in it** |
| 32 records, about 1.6 KB each | When full, new packets are refused and logged. At one packet per node per hour, that may fill quickly during an outage |

ARQ protects data against network failures, not against power loss. If that matters for your deployment, write your own buffer. Changing `kQueueRecords` is a smaller option, but keep RAM free for HTTPS, which needs tens of KB per connection.

### Writing your own buffer

Flash (LittleFS) or an SD card works. Storing raw packets (about 214 bytes) rather than floats (about 1.6 KB) holds about 7 times as many, and you can convert them inside `peek()`.

Implement this interface from `lib/nakhel_uplink/src/uplink_queue.h`:

```cpp
class MyBuffer : public nakhel::UplinkQueue {
 public:
  bool peek(nakhel::UplinkRecord& out) override;  // copy the oldest record; don't remove it
  void remove() override;                         // now remove that record
  size_t count() override;                        // how many are waiting
};
```

Then in `src/main.cpp`, pass it to `Uplink` in place of `uplinkQueue`, and have your receiver add to it.

**ARQ depends on these rules:**

1. `peek()` returns the same record, with the same values, every time until `remove()` is called. The record leaves the buffer only after the server acknowledges it.
2. Oldest first.
3. **It must be thread-safe.** The uplink runs on core 0 while your receiver adds records on core 1, so use a mutex, as `RamQueue` does. The uplink calls `peek()` often, so don't hold the mutex for long.
4. Never overwrite a record that's waiting, including the one being sent.
5. A reboot between the server's ACK and `remove()` is fine: the record is resent and answered `duplicate`.

Flash wears out with repeated writes, so avoid rewriting the same area for every packet. An append-only log file with a saved read position is a common approach.

## How the uplink behaves

You shouldn't need to change any of this, but it helps when debugging.

| Server answer to a record | What the uplink does |
|---|---|
| 201 `stored` or 200 `duplicate`, naming the same node and timestamp | Removes it and sends the next one |
| 400, 413 or 422 with an error code from the API | Removes it, logs it and calls `onRejected`, because retrying can't help |
| 401 `unauthorized` | Stops sending and keeps the buffer. Needs a new key and a restart |
| Anything else, including no answer | Keeps it and retries after 5 s, doubling up to 10 min, forever |

- One record is in flight at a time, so a record that keeps failing holds up the ones behind it.
- A send can block for up to 20 s, which is why the uplink has its own task.
- Nothing is sent until Wi-Fi is connected and the clock is set.
- Cluster health goes out every 5 minutes, even during a backlog. If it stops for 15 minutes, the dashboard raises a `cluster_offline` alert and emails the owners.

## Project layout

| Path | What | Change it? |
|---|---|---|
| `src/lora_receiver.cpp` | Radio to buffer | **Yes: your code goes here** |
| `src/app_queue.h` | Buffer type and size | If you change the buffer |
| `src/main.cpp` | Startup, Wi-Fi, NTP, tasks | If you change the buffer |
| `src/esp32_transport.*` | HTTP(S) | No |
| `lib/nakhel_uplink/src/records.h` | The record fields | No |
| `lib/nakhel_uplink/src/uplink_queue.h` | Buffer interface | No |
| `lib/nakhel_uplink/src/` (the rest) | ARQ and JSON | No |
| `test/` | Unit tests | Add yours |

## Tests

These need no board, but do need a C++ compiler on your computer:

```bash
pio test -e native
```

The full server API is in [`docs/contracts/cluster_head_to_cloud.md`](https://github.com/HaithamAyyash/rpw-dashboard/blob/main/docs/contracts/cluster_head_to_cloud.md) in the `rpw-dashboard` repo.
