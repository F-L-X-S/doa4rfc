# doa4rfc
#### Realtime Direction-of-Arrival Estimation for RF Communication Protocols

## Objective  
This project aims to provide a flexible software architecture to implement and test DoA methods for various RF communication protocols.

## Main Features
- [Main Application](src/main.cc): Estimation of the DoA of a transmitted OFDM / singlecarrier signal

### Multichannel Frame Synchronization 
-  [MultiSync](include/multisync/README.md) for simultaneous processing and phase offset correction with multiple generic frame synchronizers based on [Liquid-DSP](https://liquidsdr.org)

### Multithread Architecture 
- [Sync-Worker](include/sync_worker/include/sync_worker.h): Multichannel frame detection and synchronization
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
- [DoA-Estimation with MUSIC for Single- and Multicarrier Signals](simulations/music/README.md) 
- [Single-Channel CFR-Estimation](simulations/sim_singlechannel/README.md) 
- [Multi-Channel CFR-Estimation](simulations/sim_multichannel/README.md)
 
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
 subgraph T_ZmqTxWorker["ZMQ-TX-Worker"]
        ZmqSocket["ZMQ Socket"]
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
    MultiChSampsQueue -- Provide Multi-Ch Samples ---> ZmqSocket
    MultiChSampsQueue -- Provide Multi-Ch Samples ---> MatlabExport
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
The following diagram illustrates, how samples are streamed from the two SDR-instances, synchronized as sample-blocks with a unique timestamp based on the SDRs device-time and how the frame samples from detected frames are grouped across channels and forwarded to the MUSIC-algorithm.
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

subgraph ZmqTxWorker["ZMQ-TX-Worker"]
    ZmqExport["ZMQ Export"]
end

Socket["TCP Socket"]

subgraph DoAAlgorithm["DoA Estimation"]
    ZmqImport["ZMQ Import"]
    MusicAlg["MUSIC Algorithm"]
end

Sdr1 -- "Sample-Stream" ---> Rx1
Sdr2 -- "Sample-Stream" ---> Rx2

Sdr1 -- "Timestamp" ---> Rx1
Sdr2 -- "Timestamp" ---> Rx2

Rx1 -- "SampleBlock_t" ---> MultiSync
Rx2 -- "SampleBlock_t" ---> MultiSync

MultiSync -- "FrameSamps_t" ---> Grouping
MultiSync -- "FrameSyms_t" ---> Grouping
Grouping -- "Samples_2dim_t" ---> ZmqExport
Grouping -- "Samples_2dim_t" ---> MatlabExport
Grouping -- "Symbols_2dim_t" ---> MatlabExport
ZmqExport -- "Samples_2dim_t" ---> Socket
Socket -- "Samples_2dim_t [1..*]" ---> ZmqImport
ZmqImport -- "Samples_2dim_t [1..*]" ---> MusicAlg

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

#### How to install the doa4rfc Gnuradio-OOT module 
 1. `cd` to `gr-doa4rfc/build` (create, if not existing) <br>
 2. Run `cmake -DCMAKE_INSTALL_PREFIX=/opt/homebrew ../` (replace with your grc path, since gnuradio blocks are shared libraries the .dylibs will be placed in /lib of the specified directory)<br>
 3. Run `make -j && sudo make install`<br>
 4. Open gnuradio-companion and refresh your blocks. A new doa4rfc section should appear below the core module. 

### Main Dependencies
- [ZMQ](https://zeromq.org/languages/cplusplus/) for socket communication with the Python-implemented DoA Algorithm 
- [Liquid-DSP](https://liquidsdr.org) for frame-detection, generation and synchronization
- [UHD](https://files.ettus.com/manual/index.html) for USRP communication
