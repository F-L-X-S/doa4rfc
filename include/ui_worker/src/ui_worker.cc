/**
 * @file ui_worker.cc
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Terminal-based UI worker with extensible command registry
 * @version 0.2
 * @date 2026-03-25
 *
 *
 */

#include <ui_worker.h>

TerminalWorker::TerminalWorker(std::atomic<bool>& stop_signal_ref):
    MultithreadWorker(stop_signal_ref)
{
    RegisterBuiltinCommands();
};

TerminalWorker::~TerminalWorker(){};

void TerminalWorker::RegisterCommand(const std::string& name, Command cmd) {
    commands_[name] = std::move(cmd);
};

void TerminalWorker::SetPhaseCorrQueue(PhaseQueue_t* queue) {
    phase_queue_ = queue;
};

void TerminalWorker::SetMatlabWorker(MatlabWorker* worker) {
    matlab_worker_ = worker;
};

void TerminalWorker::Execute() {
    std::cout << "\n"
              << R"(       /$$                     /$$   /$$            /$$$$$$         )" << "\n"
              << R"(      | $$                    | $$  | $$           /$$__  $$        )" << "\n"
              << R"(  /$$$$$$$  /$$$$$$   /$$$$$$ | $$  | $$  /$$$$$$ | $$  \__//$$$$$$$)" << "\n"
              << R"( /$$__  $$ /$$__  $$ |____  $$| $$$$$$$$ /$$__  $$| $$$$   /$$_____/)" << "\n"
              << R"(| $$  | $$| $$  \ $$  /$$$$$$$|_____  $$| $$  \__/| $$_/  | $$      )" << "\n"
              << R"(| $$  | $$| $$  | $$ /$$__  $$      | $$| $$      | $$    | $$      )" << "\n"
              << R"(|  $$$$$$$|  $$$$$$/|  $$$$$$$      | $$| $$      | $$    |  $$$$$$$)" << "\n"
              << R"( \_______/ \______/  \_______/      |__/|__/      |__/     \_______/)" << "\n"
              << "\n"
              << "  Realtime Direction-of-Arrival Estimation for RF Communication Protocols\n"
              << "  (c) Felix Schuelke | MIT License\n"
              << "\n"
              << "  Type 'help' for available commands.\n"
              << std::endl;

    while (!stop_signal_called->load()) {
        std::string input;
        if (!std::getline(std::cin, input)) break;

        std::vector<std::string> tokens;
        boost::split(tokens, input, boost::is_any_of(" "), boost::token_compress_on);
        if (tokens.empty() || tokens[0].empty()) continue;

        if (tokens[0] == "exit" || tokens[0] == "quit" || tokens[0] == "q") {
            stop_signal_called->store(true);
            break;
        }

        auto it = commands_.find(tokens[0]);
        if (it == commands_.end()) {
            std::cerr << "Unknown command: " << tokens[0]
                      << ". Type 'help' for available commands." << std::endl;
            continue;
        }

        if (tokens.size() < it->second.min_args) {
            std::cerr << "Usage: " << it->second.usage << std::endl;
            continue;
        }

        auto err = it->second.handler(tokens);
        if (!err.empty()) std::cerr << "Error: " << err << std::endl;
    }
};

void TerminalWorker::RegisterBuiltinCommands() {
    RegisterCommand("help", {
        .usage = "help",
        .description = "List available commands",
        .min_args = 1,
        .handler = [this](const std::vector<std::string>&) -> std::string {
            std::cout << "Available commands:" << std::endl;
            for (const auto& [name, cmd] : commands_)
                std::cout << "  " << cmd.usage << " — " << cmd.description << std::endl;
            std::cout << "  exit | quit | q — Terminate program" << std::endl;
            return "";
        }
    });

    RegisterCommand("matlab", {
        .usage = "matlab <on|off|single>",
        .description = "Control MATLAB export: on (continuous), off (disabled), single (export next frame only)",
        .min_args = 2,
        .handler = [this](const std::vector<std::string>& tokens) -> std::string {
            if (!matlab_worker_) return "No MATLAB worker connected";
            if (tokens[1] == "on") {
                matlab_worker_->SetExportEnabled(true);
                std::cout << "MATLAB export enabled" << std::endl;
            } else if (tokens[1] == "off") {
                matlab_worker_->SetExportEnabled(false);
                std::cout << "MATLAB export disabled" << std::endl;
            } else if (tokens[1] == "single") {
                matlab_worker_->ExportSingle();
                std::cout << "MATLAB export: capturing next single frame" << std::endl;
            } else {
                return "Unknown option '" + tokens[1] + "'. Use on, off, or single";
            }
            return "";
        }
    });

    RegisterCommand("adjust_phase", {
        .usage = "adjust_phase <channel> <phase_rad>",
        .description = "Increment NCO phase of a specific channel by the given value [rad]",
        .min_args = 3,
        .handler = [this](const std::vector<std::string>& tokens) -> std::string {
            if (!phase_queue_) return "No phase correction queue connected";
            Phase_t p;
            p.channel = std::stoi(tokens[1]);
            p.phi = static_cast<float>(std::stod(tokens[2]));
            p.absolute = false;
            PushItemToQueue(*phase_queue_, std::move(p));
            return "";
        }
    });

    RegisterCommand("set_phase", {
        .usage = "set_phase <channel> <phase_rad>",
        .description = "Set NCO phase of a specific channel to an absolute value [rad]",
        .min_args = 3,
        .handler = [this](const std::vector<std::string>& tokens) -> std::string {
            if (!phase_queue_) return "No phase correction queue connected";
            Phase_t p;
            p.channel = std::stoi(tokens[1]);
            p.phi = static_cast<float>(std::stod(tokens[2]));
            p.absolute = true;
            PushItemToQueue(*phase_queue_, std::move(p));
            return "";
        }
    });
};
