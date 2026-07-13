# doa4rfc
#### Realtime Direction-of-Arrival Estimation for RF Communication Protocols

## Objective  
This project aims to provide a flexible software architecture to implement and test DoA methods for various RF communication protocols.

## Main Features
- [Main Application](src/main.cc): Estimation of the DoA of a transmitted OFDM / singlecarrier signal

### Multichannel Frame Synchronization 
-  [MultiSync](include/multisync/README.md) for simultaneous processing and phase offset correction with multiple generic frame synchronizers based on [Liquid-DSP](https://liquidsdr.org)

### Multithread Architecture 
- [Sync-Worker](include/sync_worker/README.md): Multichannel frame detection and synchronization
- [Grouping-Worker](include/grouping_worker/include/grouping_worker.h): Identification of frames related across channels
- [UI-Worker](include/ui_worker/include/ui_worker.h): Provision of terminal interface 

### DoA Estimation Algorithms
- [MUSIC Algorithm](music/music-spectrum.py) (multiple signal classification) python app based on [pyespargos](https://github.com/ESPARGOS/pyespargos) 

### Interfaces 
- [ZMQ TCP interface](include/interfaces/zmq/README.md): Standardized TCP transmission of multidimensional sample-vectors
- [MATLAB interface](include/interfaces/matlab/include/matlab_if.h): Generation of .m files for plotting signals and constellation diagrams in MATLAB (check [matlabXport](https://github.com/F-L-X-S/matlabXport))
- [UHD interface](include/interfaces/uhd/include/uhd_if.h): Hardware interface for USRP SDRs (especially N210)

## Simulations
 [Simulations](simulations/) provided in ./simulations demonstrate the usage of the provided modules, illustrate the underlying mathematical concepts and show the simulation results:
- [Genereic Singlecarrier and OFDM Signal DoA Estimation](simulations/music/README.md) 
- [IEEE 802.11n WiFi DoA Estimation](simulations/wifi/wifi_sim.cc)
- [GPS L1 C/A DoA Estimation](simulations/gps/gps_sim.cc)
- [DVB-S2 DoA Estimation](simulations/dvbs2/dvbs2_sim.cc)
- [GNU Radio Live DoA Estimation](simulations/gnuradio/gnuradio_sim.cc)
- [Single-Channel CFR-Estimation](simulations/sim_singlechannel/README.md) 
- [Multi-Channel CFR-Estimation](simulations/sim_multichannel/README.md)
### Simulation Process Flow
All protocol simulations share the processing pipeline of the main application, but replace the SDR hardware interface by the ZMQ import socket (`tcp://127.0.0.1:5554`). The multi-channel baseband frames are generated per simulation either internally with Liquid-DSP (`music_sim`), loaded from a MATLAB-generated ULA dataset file (`wifi_sim`, `gps_sim`, `dvbs2_sim`), or streamed live by an external application — a GNU Radio flowgraph using the [zmq_if_sink block](gnuradio/README.md) (`gnuradio_sim`) or any process pushing the [ZMQ wire format](include/interfaces/zmq/README.md). Each worker runs in a separate thread and is decoupled by thread-safe queues; the DoA application runs as a separate Python process.
```mermaid
---
config:
  look: classic
  layout: elk
  theme: redux
---
flowchart TD
 subgraph FrameSources["Frame Generation (one alternative per simulation)"]
        LiquidGen["Liquid-DSP Frame Generator (OFDM / Flexframe) [music_sim]"]
        ChannelSim["Multipath Channel Simulation (Differential Delay ≙ DoA, AWGN)"]
        MatlabGen["MATLAB ULA Dataset Generator (*_dataset_gen.m, offline)"]
        BinFile["Binary Dataset (records/*.bin)"]
        LoadDataset["Dataset Loader (cyclic replay) [wifi_sim, gps_sim, dvbs2_sim]"]
        GrFlowgraph["GNU Radio Flowgraph (gr-digital OFDM blocks, Steering Phases ≙ DoA) [gnuradio_sim]"]
        ZmqIfSink["zmq_if_sink GRC Block"]
        PyOther["Any external Application (Python, ...)"]
  end
 subgraph T_ZmqTxExtWorker["ZMQ-TX-Worker (simulated external source)"]
        ZmqTxExt["ZMQ Socket"]
  end
 subgraph T_ZmqRxWorker["ZMQ-RX-Worker"]
        ZmqRx["ZMQ Import + Header Parsing"]
  end
 subgraph T_SyncWorker["Sync-Worker"]
        MultiSync["Multi-Channel Synchronization (ofdmflexframesync | wlanframesync | scframesync)"]
        PhiErrorCorrection["Phase Offset Correction"]
  end
 subgraph T_GroupingWorker["Grouping-Worker"]
        FindGroups["Time-based Grouping"]
  end
 subgraph T_MatlabWorker["MATLAB-Worker"]
        MatlabExport["MATLAB Export (*_sim.m)"]
  end
 subgraph T_ZmqTxSampsWorker["ZMQ-TX-Worker (Samples)"]
        ZmqSampsSocket["ZMQ Socket (msg-type: Samples)"]
  end
 subgraph T_ZmqTxSymsWorker["ZMQ-TX-Worker (Symbols)"]
        ZmqSymsSocket["ZMQ Socket (msg-type: Symbols)"]
  end
 subgraph T_TerminalWorker["Terminal-Worker"]
        ReadInput["Read Terminal Inputs"]
        CommandRegistry["Command Registry"]
  end
 subgraph PyApp["Python DoA-App (separate process)"]
        FrameReceiver["ZMQ Import (FrameReceiver)"]
        Estimator["DoA Estimator (MUSIC, interchangeable)"]
        FramePlots["Per-Frame Plots (Time, Magnitude, FFT, Constellation)"]
  end
    ImportSocket["TCP Socket tcp://127.0.0.1:5554"]
    ExportSocket["TCP Socket tcp://127.0.0.1:5555"]
    LiquidGen -- Baseband Frame --> ChannelSim
    ChannelSim -- Push Multi-Ch Sequence --> TxQueueExt["External TX Queue"]
    MatlabGen -- Generate offline ---> BinFile
    BinFile -- Load at Startup --> LoadDataset
    LoadDataset -- Push Multi-Ch Frames --> TxQueueExt
    TxQueueExt -- Provide Sequences --> ZmqTxExt
    ZmqTxExt -- Push Samples --> ImportSocket
    GrFlowgraph -- IQ Streams [0..N-1] --> ZmqIfSink
    ZmqIfSink -- Push Samples --> ImportSocket
    PyOther -- Push Samples --> ImportSocket
    ImportSocket --> ZmqRx
    ZmqRx -- Push Timestamped Sample Blocks --> RxSampleQueue["RX Sample Queue [0..N-1]"]
    RxSampleQueue -- Provide Sample Blocks ---> MultiSync
    PhiErrorCorrection -- Correct Phase ---> MultiSync
    MultiSync -- Push Frame Samples ---> FrameSampsQueue["Frame Samples Queue"]
    MultiSync -- Push Frame Symbols ---> FrameSymsQueue["Frame Symbols Queue"]
    FrameSampsQueue -- Provide Frame Samples ---> FindGroups
    FrameSymsQueue -- Provide Frame Symbols ---> FindGroups
    FindGroups -- Push Multi-Ch Samples ---> MultiChSampsQueue["Multi-Ch Samples Queue"]
    FindGroups -- Push Multi-Ch Symbols ---> MultiChSymsQueue["Multi-Ch Symbols Queue"]
    MultiChSampsQueue -- Provide Multi-Ch Samples ---> ZmqSampsSocket & MatlabExport
    MultiChSymsQueue -- Provide Multi-Ch Symbols ---> ZmqSymsSocket & MatlabExport
    ZmqSampsSocket -- Push Samples --> ExportSocket
    ZmqSymsSocket -- Push Symbols --> ExportSocket
    ExportSocket --> FrameReceiver
    FrameReceiver -- CSI --> Estimator
    FrameReceiver -- Latest Frame --> FramePlots
    ReadInput --> CommandRegistry
    CommandRegistry -- Push Phase Offset ---> PhiErrorQueue["Phase Offset Queue"]
    PhiErrorQueue -- Provide Phase Offset ---> PhiErrorCorrection
    CommandRegistry -- Control Export ---> MatlabExport
    CommandRegistry -- Triggers ---> Exit["Exit"]
```

### Simulation Data-flow
The diagram below shows the data formats along the pipeline: all ZMQ messages carry the wire format `4 x uint32 header [msg_type, n_measurements, n_channels, n_samples]` followed by the `complex64` payload (row-major `[measurement][channel][sample]`). The ZMQ-RX-Worker decomposes each message into per-channel `SampleBlock_t` items with a shared receive-timestamp used by the Grouping-Worker to relate frames across channels. The synchronizer type is selected per simulation via `SyncTraits` (`ofdmflexframesync` — music, `wlanframesync` — wifi/gnuradio, `scframesync` — gps/dvbs2).
```mermaid
---
config:
  look: classic
  layout: elk
  theme: redux
---
flowchart TD
subgraph Sources["Frame Generation"]
    Liquid["music_sim: Liquid-DSP Framegen + Channel Sim"]
    Dataset["wifi/gps/dvbs2_sim: MATLAB Dataset (records/*.bin)"]
    Gnuradio["gnuradio_sim: GNU Radio Flowgraph + zmq_if_sink"]
end

subgraph ZmqTxExtWorker["ZMQ-TX-Worker (sim-internal)"]
    ZmqTxExt["ZMQ Export"]
end

ImportSocket["TCP Socket :5554"]

subgraph ZmqRxWorker["ZMQ-RX-Worker"]
    ZmqRx["ZMQ Import"]
end

subgraph SyncWorker["Sync-Worker"]
    MultiSync["MultiSync"]
end

subgraph GroupingWorker["Grouping-Worker"]
    Grouping["Time-based Grouping"]
end

subgraph ZmqTxSampsWorker["ZMQ-TX-Worker (Samples)"]
    ZmqExportSamps["ZMQ Export"]
end

subgraph ZmqTxSymsWorker["ZMQ-TX-Worker (Symbols)"]
    ZmqExportSyms["ZMQ Export"]
end

subgraph MatlabWorker["MATLAB-Worker"]
    MatlabExport["MATLAB Export"]
end

ExportSocket["TCP Socket :5555"]

subgraph DoAApp["Python DoA-App (music-spectrum.py)"]
    ZmqImport["ZMQ Import (FrameReceiver)"]
    MusicAlg["DoA Estimator (MUSIC, interchangeable)"]
    FramePlots["Per-Frame Plots (Time, Magnitude, FFT, Constellation)"]
end

Liquid -- "Samples_2dim_t [n_ch][n_samp]" ---> ZmqTxExt
Dataset -- "Samples_2dim_t (per frame)" ---> ZmqTxExt
ZmqTxExt -- "header + complex64 (msg-type: Samples)" ---> ImportSocket
Gnuradio -- "header + complex64 (chunk-wise)" ---> ImportSocket
ImportSocket ---> ZmqRx
ZmqRx -- "SampleBlock_t {Samples_1dim_t, timestamp} [0..N-1]" ---> MultiSync
MultiSync -- "FrameSamps_t" ---> Grouping
MultiSync -- "FrameSyms_t" ---> Grouping
Grouping -- "Samples_2dim_t" ---> ZmqExportSamps & MatlabExport
Grouping -- "Symbols_2dim_t" ---> ZmqExportSyms & MatlabExport
ZmqExportSamps -- "msg-type: Samples" ---> ExportSocket
ZmqExportSyms -- "msg-type: Symbols" ---> ExportSocket
ExportSocket -- "header + complex64 [1..*]" ---> ZmqImport
ZmqImport -- "CSI (n_meas, 1, 1, n_ch, n_samp)" ---> MusicAlg
ZmqImport -- "Latest Frame (n_ch, n_samp)" ---> FramePlots
MatlabExport -- "Samples + Symbols" ---> MFile[("*_sim.m")]
```

### Adding a Custom Synchronizer
The synchronizer type used by the [Sync-Worker](include/sync_worker/include/sync_worker.h) is exchangeable via template traits: [MultiSync](include/multisync/include/multisync.h) never calls a Liquid-DSP synchronizer directly, but only through a `SyncTraits<>` specialization ([synctraits.h](include/multisync/include/synctraits.h)) that maps the six required operations (`Create`, `Reset`, `Execute`, `Destroy`, `GetFrameLen`, `GetFrameSym`) plus the frame-detection callback to the concrete C-API. The C++20 concept `SyncTraitsConcept` verifies at compile time that a specialization provides the complete interface. This is how the custom `wlanframesync` (used by `wifi_sim`/`gnuradio_sim`) and `scframesync` (used by `gps_sim`/`dvbs2_sim`) were added alongside the stock Liquid-DSP synchronizers.

The class- and template-dependencies are shown below: `SyncWorker` owns a `MultiSync` instance, which is generic over the synchronizer interface; the interface is chosen at compile time by passing a `SyncTraits` specialization (its `_iface` alias) as template argument.
```mermaid
classDiagram
    direction LR

    class MultithreadWorker {
        <<abstract>>
        +RunWorker()
        +StopWorker()
        #Execute()*
        #AddWorkerQueue(queue)
    }

    class ThreadSafeQueue~T~ {
        +push(item)
        +pop(buffer) bool
    }

    class SyncWorker~num_channels, synchronizer_iface~ {
        +SyncWorker(MsCreateParams_t params, atomic_bool stop, int record_padding)
        +GetRxQueues()
        +GetPhaseCorrQueue()
        +AddFrameSampsQueue(queue)
        +AddFrameSymsQueue(queue)
        -callback(userdata)$ int
        -MultiSync ms_
        -CallbackData_t cb_data_
    }

    class MultiSync~synchronizer_interface, num_channels~ {
        +MultiSync(CreateParams_t params, GenericCallback_t handler, userdata_per_channel)
        +Execute(channel_samples, record_index)
        +Reset()
        +SetNcoPhase(channel, phi)
        +GetFrameLen(channel) unsigned int
        +GetFrameSyms(channel, syms)
        +GetMultiChannelFrameSamps() Samples_2dim_t
        -SynchronizerType framesync_
        -nco_crcf nco_
        -CallbackWrapper cb_wrappers_
    }

    class CallbackWrapper {
        +GenericCallback_t handler
        +void* userdata
    }

    class SyncTraitsConcept {
        <<concept>>
    }

    class SyncTraits~SynchronizerType~ {
        <<template>>
        +SynchronizerType
        +CreateParams_t
        +Callback(args, userdata)$ int
        +Create(params, wrapper)$ SynchronizerType
        +Reset(fs)$ void
        +Execute(fs, x, n)$ int
        +Destroy(fs)$ void
        +GetFrameLen(fs)$ unsigned int
        +GetFrameSym(fs, y, pos)$ unsigned int
    }

    class ofdmframesync_iface
    class ofdmflexframesync_iface
    class flexframesync_iface
    class wlanframesync_iface
    class scframesync_iface

    class LiquidDsp["Liquid-DSP C-API"]
    class CustomSync["Custom C synchronizers in include/synchronizer"]

    MultithreadWorker <|-- SyncWorker : inherits
    MultithreadWorker o-- ThreadSafeQueue : registered worker queues
    SyncWorker "1" *-- "1" MultiSync : ms_
    SyncWorker ..> SyncTraits : synchronizer_iface template argument
    MultiSync "1" *-- "num_channels" CallbackWrapper : cb_wrappers_
    MultiSync ..> SyncTraitsConcept : template parameter constrained by
    SyncTraitsConcept ..> SyncTraits : verifies static interface of
    SyncTraits <|.. ofdmframesync_iface : specialization
    SyncTraits <|.. ofdmflexframesync_iface : specialization
    SyncTraits <|.. flexframesync_iface : specialization
    SyncTraits <|.. wlanframesync_iface : specialization
    SyncTraits <|.. scframesync_iface : specialization
    ofdmframesync_iface ..> LiquidDsp : wraps
    ofdmflexframesync_iface ..> LiquidDsp : wraps
    flexframesync_iface ..> LiquidDsp : wraps
    wlanframesync_iface ..> CustomSync : wraps
    scframesync_iface ..> CustomSync : wraps
```

To add your own synchronizer:

1. **Provide the synchronizer implementation** with a Liquid-DSP-style C interface (`<name>_create`, `_reset`, `_execute`, `_destroy`, `_get_frame_len`, `_get_sym` and a `framesync_callback`-style callback), e.g. as a C-file in [include/synchronizer/](include/synchronizer/) like [wlanframesync.c](include/synchronizer/src/wlanframesync.c). Any existing Liquid-DSP synchronizer works as-is.

2. **Specialize `SyncTraits<>`** for the new type in [synctraits.h](include/multisync/include/synctraits.h):
   ```cpp
   template<>
   struct SyncTraits<myframesync> {
       using SynchronizerType = myframesync;
       struct CreateParams_t { myframesync_config_t config; };  // everything Create() needs

       // C-callback matching the synchronizer's callback signature:
       // unwrap the CallbackWrapper and forward to the generic handler
       static int Callback(/* synchronizer-specific args */, void* _userdata) {
           auto* w = static_cast<CallbackWrapper*>(_userdata);
           return w->handler(w->userdata);
       };

       static SynchronizerType Create(const CreateParams_t& p, CallbackWrapper* w)
           { return myframesync_create(&p.config, Callback, w); };
       static void Reset(SynchronizerType fs)   { myframesync_reset(fs); };
       static int Execute(SynchronizerType fs, Sample_t* x, unsigned int n)
           { return myframesync_execute(fs, reinterpret_cast<liquid_float_complex*>(x), n); };
       static void Destroy(SynchronizerType fs) { myframesync_destroy(fs); };
       static unsigned int GetFrameLen(SynchronizerType fs)
           { return myframesync_get_frame_len(fs); };
       static unsigned int GetFrameSym(SynchronizerType fs, Symbol_t* x, unsigned int pos)
           { return myframesync_get_sym(fs, liquid_conv::Ptr(x), pos); };
   };
   using myframesync_iface = SyncTraits<myframesync>;
   ```
   The `Callback` returns the handler's value to the synchronizer — return `1` there to reset the synchronizer after a frame (see `SyncWorker::callback`).

3. **Instantiate the Sync-Worker** with the new interface as template argument; the `CreateParams_t` are passed through to `Create()` for every channel (one synchronizer + NCO instance per channel is created inside `MultiSync`):
   ```cpp
   SyncWorker<NUM_CHANNELS, myframesync_iface> sync(
       {{/* CreateParams_t, e.g. myframesync_config_t */}},
       std::ref(stop_signal_called), 0 /* record padding [samples] */);
   ```
   See [gnuradio_sim.cc](simulations/gnuradio/gnuradio_sim.cc) for a complete example that configures `wlanframesync_iface` (subcarrier allocation, STF/LTF sequences, pilot pattern) to detect frames generated by GNU Radio. If the specialization misses a function or a signature differs, `SyncTraitsConcept` rejects the instantiation with a compile-time error.

## Measurements
[Measurements](measurements/) show the real-world DoA results of the [Application](src/main.cc) using two USRP N210 with WBX daughterboard and provide the corresponding datasets.

## DoA estimation with USRP N210
 The [Main Application](src/main.cc) gives you a functioning example on how to employ the provided modules for DoA estimation with USRP N210. 
![Demo of DoA Estimation](https://github.com/F-L-X-S/doa4rfc/raw/main/docs/assets/doa4rfc.gif)

### Main App Process Flow
```mermaid
---
config:
  look: classic
  layout: elk
  theme: redux
---
flowchart TD
FrameGen["Frame Generator"]
subgraph HardwareInterface["SDR Hardware Interface"]
 subgraph T_StreamWorker["Stream-Worker"]
        UsrpDevices["USRP Device Interface [0..*]"]
        UsrpConf["USRP Interface Setup"]
        StreamConf["Timed Stream Command"]
  end
 subgraph T_RxWorker["RX-Worker [0..*]"]
        RxStream["RX Stream Interface"]
        SampleBlock["Sample Block Buffer"]
  end
 subgraph T_TxWorker["TX-Worker"]
        TxStream["TX Stream Interface"]
        TxBuffer["TX Buffer"]
  end
end
 subgraph T_SyncWorker["Sync-Worker"]
        MultiSync["Multi-Channel Synchronization"]
        PhiErrorCorrection["Phase Offset Correction"]
  end
 subgraph T_GroupingWorker["Grouping-Worker"]
        FindGroups["Time-based Grouping"]
  end
 subgraph T_MatlabWorker["MATLAB-Worker"]
        MatlabExport["MATLAB Export"]
  end
 subgraph T_ZmqTxSampsWorker["ZMQ-TX-Worker (Samples)"]
        ZmqSampsSocket["ZMQ Socket (msg-type: Samples)"]
  end
 subgraph T_ZmqTxSymsWorker["ZMQ-TX-Worker (Symbols)"]
        ZmqSymsSocket["ZMQ Socket (msg-type: Symbols)"]
  end
 subgraph T_TerminalWorker["Terminal-Worker"]
        ReadInput["Read Terminal Inputs"]
        CommandRegistry["Command Registry"]
  end
    UsrpConf -- Configure Interfaces ---> UsrpDevices
    UsrpDevices -- Provide Device Time --> StreamConf
    StreamConf -- Issue Command  --> UsrpDevices
    UsrpDevices -- Provide Stream Instance ---> RxStream & TxStream
    RxStream -- Forward Samples --> SampleBlock
    RxStream -- Provide Timestamp --> SampleBlock
    SampleBlock -- Push Sample Block --> RxSampleQueue["RX Sample Queue [0..*]"]
    RxSampleQueue -- Provide Sample Blocks ---> MultiSync
    PhiErrorCorrection -- Correct Phase ---> MultiSync
    MultiSync -- Push Frame Samples ---> FrameSampsQueue["Frame Samples Queue"]
    MultiSync -- Push Frame Symbols ---> FrameSymsQueue["Frame Symbols Queue"]
    FrameSampsQueue -- Provide Frame Samples ---> FindGroups
    FrameSymsQueue -- Provide Frame Symbols ---> FindGroups
    FindGroups -- Push Multi-Ch Samples ---> MultiChSampsQueue["Multi-Ch Samples Queue"]
    FindGroups -- Push Multi-Ch Symbols ---> MultiChSymsQueue["Multi-Ch Symbols Queue"]
    MultiChSampsQueue -- Provide Multi-Ch Samples ---> ZmqSampsSocket
    MultiChSymsQueue -- Provide Multi-Ch Symbols ---> ZmqSymsSocket
    MultiChSymsQueue -- Provide Multi-Ch Symbols ---> MatlabExport
    FrameGen -- Write Content ---> TxBuffer
    TxStream -- Transmit Content ---> TxBuffer
    ReadInput -----> CommandRegistry
    CommandRegistry -- Push Phase Offset ---> PhiErrorQueue["Phase Offset Queue"]
    PhiErrorQueue -- Provide Phase Offset ---> PhiErrorCorrection
    CommandRegistry -- Control Export ---> MatlabExport
    CommandRegistry -- Triggers ---> Exit["Exit Streaming"]

```
### Main App Data-flow
The following diagram illustrates, how samples are streamed from the two SDR-instances, synchronized as sample-blocks with a unique timestamp based on the SDRs device-time and how the frame samples and demodulated symbols from detected frames are grouped across channels and forwarded to the Python DoA-application (both message types share one ZMQ socket, distinguished by the msg-type header field).
```mermaid
---
config:
  look: classic
  layout: elk
  theme: redux
---
flowchart TD
Sdr1["SDR Channel 1"]
Sdr2["SDR Channel 2"]
subgraph HardwareInterface["SDR Hardware Interface"]
        Rx1["RX Channel 1"]
        Rx2["RX Channel 2"]
end

subgraph SyncWorker["Sync-Worker"]
    MultiSync["MultiSync"]
end

subgraph GroupingWorker["Grouping-Worker"]
    Grouping["Time-based Grouping"]
end

subgraph MatlabWorker["MATLAB-Worker"]
    MatlabExport["MATLAB Export"]
end

subgraph ZmqTxSampsWorker["ZMQ-TX-Worker (Samples)"]
    ZmqExportSamps["ZMQ Export"]
end

subgraph ZmqTxSymsWorker["ZMQ-TX-Worker (Symbols)"]
    ZmqExportSyms["ZMQ Export"]
end

Socket["TCP Socket"]

subgraph DoAAlgorithm["Python DoA-App (music-spectrum.py)"]
    ZmqImport["ZMQ Import (FrameReceiver)"]
    MusicAlg["DoA Estimator (MUSIC, interchangeable)"]
    FramePlots["Per-Frame Plots (Time, Magnitude, FFT, Constellation)"]
end

Sdr1 -- "Sample-Stream" ---> Rx1
Sdr2 -- "Sample-Stream" ---> Rx2

Sdr1 -- "Timestamp" ---> Rx1
Sdr2 -- "Timestamp" ---> Rx2

Rx1 -- "SampleBlock_t" ---> MultiSync
Rx2 -- "SampleBlock_t" ---> MultiSync

MultiSync -- "FrameSamps_t" ---> Grouping
MultiSync -- "FrameSyms_t" ---> Grouping
Grouping -- "Samples_2dim_t" ---> ZmqExportSamps
Grouping -- "Symbols_2dim_t" ---> ZmqExportSyms
Grouping -- "Symbols_2dim_t" ---> MatlabExport
ZmqExportSamps -- "msg-type: Samples" ---> Socket
ZmqExportSyms -- "msg-type: Symbols" ---> Socket
Socket -- "4x uint32 header + complex64 [1..*]" ---> ZmqImport
ZmqImport -- "CSI (n_meas, 1, 1, n_ch, n_samp)" ---> MusicAlg
ZmqImport -- "Latest Frame (n_ch, n_samp)" ---> FramePlots

```

### Hardware Setup 
The software is tested using two USRP N210 with the WBXv3 daughterboard. Phase synchronization is achieved with the MIMO-cable. The USRPs are connected to the host by separate ethernet interfaces. For utilizing a different type of SDRs, the interfaces can be implemented in separated threads similar to [uhd_if.h](include/interfaces/uhd/include/uhd_if.h).  <br>
One USRP is used for transmitting and receiving the OFDM packages while the other USRP is used in RX-mode only. The MUSIC-spectrum visualizes the position of the TX-antenna. 
 <br>
Make sure, the receiving antennas are spaced by the half wavelength of the carrier frequency (e.g. 12cm for a carrier of 1.25GHz).

### Terminal Interface
The application provides an interactive terminal interface ([TerminalWorker](include/ui_worker/include/ui_worker.h)) with the following built-in commands:

| Command | Usage | Description |
|---------|-------|-------------|
| `help` | `help` | List all available commands |
| `matlab` | `matlab <on\|off\|single>` | Control MATLAB export: `on` enables continuous export, `off` disables export (prevents large .m files), `single` exports only the next received frame |
| `adjust_phase` | `adjust_phase <channel> <phase_rad>` | Increment the NCO phase of a specific channel by the given value [rad] |
| `set_phase` | `set_phase <channel> <phase_rad>` | Set the NCO phase of a specific channel to an absolute value [rad] |
| `exit` | `exit` / `quit` / `q` | Terminate the program |

Custom commands can be registered at runtime via `TerminalWorker::RegisterCommand()`.

### Installation 
1. Clone the Repo to your local machine
2. Setup a virtual environment within the `./music/`directory <br>
   ```
   cd ./music
   python -m venv env
   ```
3. Install all python dependencies specified in `requirements.txt` <br>
   ```
    source env/bin/activate
    pip install -e . 
   ``` 
4. Use the CMake extension to configure the project 
5. Set `doa4rfc` as target for build and execution (or any sim-file)
6. Go to the vscode "run and debug" menu and start the `Debug (Clang CMake Preset)` task to build and run the specified target 
<br><br>

Make sure, that all USRPs are connected via separate Ethernet interfaces, since the datarate can possibly cause overflows in the shared-Etehrnet mode. Check the USRP connection by running `uhd_find_devices`. 

#### How to add the doa4rfc GNU Radio block
The GRC block `zmq_if_sink` (streams IQ samples from a GNU Radio flowgraph to doa4rfc via ZMQ) is a lightweight pure-Python block in [gnuradio/](gnuradio/) — see [gnuradio/README.md](gnuradio/README.md) for the installation steps (block path registration and Python import setup).

### Main Dependencies
- [ZMQ](https://zeromq.org/languages/cplusplus/) for socket communication with the Python-implemented DoA Algorithm 
- [Liquid-DSP](https://liquidsdr.org) for frame-detection, generation and synchronization
- [UHD](https://files.ettus.com/manual/index.html) for USRP communication
