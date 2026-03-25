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

void TerminalWorker::Execute() {
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

    RegisterCommand("adjust_phase", {
        .usage = "adjust_phase <channel> <phase_rad>",
        .description = "Adjust phaseshift for a specific channel applied by the internal channel NCO",
        .min_args = 3,
        .handler = [this](const std::vector<std::string>& tokens) -> std::string {
            if (!phase_queue_) return "No phase correction queue connected";
            Phase_t p;
            p.channel = std::stoi(tokens[1]);
            p.phi = static_cast<float>(std::stod(tokens[2]));
            PushItemToQueue(*phase_queue_, std::move(p));
            return "";
        }
    });
};
