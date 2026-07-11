# GNU Radio integration

Custom GNU Radio Companion (GRC) block `zmq_if_sink` that streams complex IQ
samples from a flowgraph to the doa4rfc application via ZeroMQ, replacing the
simulation-internal sample source. It connects a PUSH socket to the
application's `IMPORT_INTERFACE` (default `tcp://127.0.0.1:5554`), where
doa4rfc binds the matching PULL socket (`ZmqRxWorker`).

```
GRC flowgraph (1 stream per antenna) → [zmq_if_sink] → tcp://127.0.0.1:5554 → doa4rfc SyncWorker
```

## Files

| File | Purpose |
|---|---|
| `doa4rfc_zmq_if_sink.py` | Block implementation (pure Python, `gr.sync_block`) |
| `doa4rfc_zmq_if_sink.block.yml` | GRC block description (makes the block appear in GRC) |

## Wire format

Matches `ZmqSender`/`ZmqReceiver` in `include/interfaces/zmq`:

- Header: 4 × `uint32` little-endian — `[msg_type, numMeasurements, numChannels, samplesPerChannel]`, `msg_type = 0` (samples), `numMeasurements = 1`
- Payload: `complex64` (interleaved `float32` re/im), channel-major

One message is sent per scheduler chunk. Sends are non-blocking: if doa4rfc
is not running, chunks are dropped instead of stalling the flowgraph.

## Adding the block to GRC manually

1. **Install pyzmq into GNU Radio's Python** (the block imports `zmq`).
   For a Homebrew install:

   ```bash
   $(head -1 $(which gnuradio-companion) | cut -c3-) -m pip install pyzmq
   ```

   (Resolves the Python interpreter from GRC's shebang; repeat after
   `brew upgrade gnuradio`.)

2. **Register the block path** so GRC finds the `.block.yml`. Add to
   `~/.gnuradio/config.conf`:

   ```ini
   [grc]
   local_blocks_path = /path/to/doa4rfc/gnuradio
   ```

   Alternatively set the environment variable `GRC_BLOCKS_PATH` to this
   folder before starting GRC.

3. **Make the Python module importable** when the flowgraph runs. Permanent
   (works regardless of how GRC is launched): drop a `.pth` file into the
   site-packages of GNU Radio's Python —

   ```bash
   GRPY=$(head -1 $(which gnuradio-companion) | cut -c3-)
   echo "/path/to/doa4rfc/gnuradio" > \
     $($GRPY -c "import site; print(site.getsitepackages()[0])")/doa4rfc.pth
   ```

   Alternatively per-session: `export PYTHONPATH="/path/to/doa4rfc/gnuradio:$PYTHONPATH"`
   before launching GRC, or copy `doa4rfc_zmq_if_sink.py` next to your `.grc` file.

   Note: on Homebrew, the `.pth` file and pyzmq live inside the versioned
   Cellar — repeat steps 1 and 3 after `brew upgrade gnuradio`
   (`~/.gnuradio/config.conf` survives upgrades).

The block then appears in GRC under the **[doa4rfc]** category as
`zmq_if_sink`.

> **Note:** if an older `gr-doa4rfc` OOT module was ever installed, its
> `doa4rfc_zmq_if_sink.block.yml` in the system blocks path (e.g.
> `/opt/homebrew/share/gnuradio/grc/blocks/`) shadows this block and speaks
> an outdated wire format — delete it.

## Usage

- Set **TCP Endpoint** to the `IMPORT_INTERFACE` of the doa4rfc application
  (default `tcp://127.0.0.1:5554`).
- Set **Num Channels** to the application's `NUM_CHANNELS`; the block then
  exposes one complex input per ULA antenna channel.
- Start order does not matter (PUSH reconnects), but chunks sent while
  doa4rfc is down are dropped.
- When using the block from a hand-written Python flowgraph (instead of
  GRC-generated code), keep a Python reference to the block instance for
  the lifetime of the flowgraph (e.g. `self.sink = zmq_if_sink(...)`) —
  otherwise the Python object is garbage-collected and `start()` fails.
