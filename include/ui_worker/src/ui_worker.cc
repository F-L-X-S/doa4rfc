/**
 * @file ui_worker.h
 * @author Felix Schuelke (flxscode@gmail.com)
 * 
 * @brief 
 * @version 0.1
 * @date 2025-12-27
 * 
 * 
 */

#include <ui_worker.h>

/**
 * @brief terminal_worker reads terminal inputs. 
 * Possible commands:
 * adjust_phase <channel> <phase in rad> : push phase correction for the specified channel to the phi_error_queue 
 * exit : terminate program
 * q : terminate program
 * quit : terminate program
 * 
 * @param phi_error_queue Reference to the thread-safe queue to send phase corrections for channel synchronizers
 * @param stop_signal_called Stop signal to terminate the program
 */
void terminal_worker(    PhaseQueue_t& phi_error_queue,
                        std::atomic<bool>& stop_signal_called) {
    
    while (!stop_signal_called.load()) {
        // Read User Input 
        std::string input;
        std::getline(std::cin, input);

        // Parse single token commands 
        if (input == "exit" || input == "quit" || input == "q") {
            stop_signal_called.store(true);
            break;
        }

        // Parse multi token commands 
        std::vector<std::string> tokens;
        boost::split(tokens, input, boost::is_any_of(" "));

        // Adjust NCO phase for a specific channel
        if (tokens.size() == 3 && tokens[0] == "adjust_phase") {
            // Set NCO phase for a specific channel
            unsigned int channel_id = std::stoi(tokens[1]);
                Phase_t phase_data;
                phase_data.channel = channel_id;
                phase_data.phi = static_cast<float>(std::stod(tokens[2]));
                {
                    std::lock_guard<std::mutex> lock_phi(phi_error_queue.mtx);
                    phi_error_queue.queue.push(std::move(phase_data));
                }
                phi_error_queue.cv.notify_one();
            
        } else {
            std::cerr << "Unknown command: " << input << std::endl;
        }

    }
}