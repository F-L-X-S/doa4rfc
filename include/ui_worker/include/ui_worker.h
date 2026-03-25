/**
 * @file ui_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 *
 * @brief Terminal-based UI worker with extensible command registry
 * @version 0.2
 * @date 2026-03-25
 *
 *
 */

#ifndef UI_WORKER_H
#define UI_WORKER_H

#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <iostream>

#include <boost/algorithm/string.hpp>

#include <multithread_worker.h>
#include <sync_worker.h>

/**
 * @brief Command handler signature: receives tokenized input, returns error string (empty on success)
 */
using CommandFn = std::function<std::string(const std::vector<std::string>& tokens)>;

/**
 * @brief Registered command with metadata for validation and help output
 */
struct Command {
    std::string usage;          // e.g. "adjust_phase <channel> <phase_rad>"
    std::string description;    // e.g. "Adjust NCO phase for a specific channel"
    size_t min_args;            // minimum token count (including command name)
    CommandFn handler;
};

/**
 * @brief Terminal-based UI worker with an extensible command registry.
 *
 * Reads line-based commands from stdin, tokenizes them, and dispatches
 * to registered command handlers. Built-in commands (exit, help) are
 * always available. Workers register their queues via setter methods,
 * and new commands can be added via RegisterCommand().
 */
class TerminalWorker : public MultithreadWorker {
    public:
        TerminalWorker(std::atomic<bool>& stop_signal_ref);

        ~TerminalWorker();

        /**
         * @brief Register a command with the terminal worker
         *
         * @param name Command name (first token)
         * @param cmd Command definition with handler, usage, and description
         */
        void RegisterCommand(const std::string& name, Command cmd);

        /**
         * @brief Set the phase correction queue for adjust_phase command
         *
         * @param queue Pointer to the phase correction queue
         */
        void SetPhaseCorrQueue(PhaseQueue_t* queue);

    protected:
        void Execute() override final;

    private:
        std::unordered_map<std::string, Command> commands_;

        PhaseQueue_t* phase_queue_ = nullptr;

        /**
         * @brief Register built-in commands (help, adjust_phase)
         */
        void RegisterBuiltinCommands();
};

#endif // UI_WORKER_H
