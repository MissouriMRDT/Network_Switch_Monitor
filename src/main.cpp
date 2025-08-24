/******************************************************************************
 * @brief Main application file for monitoring a network switch via SSH.
 *
 * @file main.cpp
 * @author clayjay3 (claytonraycowen@gmail.com)
 * @date 2025-08-23
 *
 * @copyright Copyright Mars Rover Design Team 2025 - All Rights Reserved
 ******************************************************************************/

#include <libssh/libssh.h>
#include <iostream>
#include <string>
#include <stdexcept>
#include <regex>
#include <thread>
#include <chrono>

#include <RoveComm/RoveCommUDP.h>

/******************************************************************************
 * @brief Class to Monitor a network switch via SSH, collecting EIGRP topology,
 * interface statistics, and performing ping tests. The collected szData is parsed
 * and displayed in a human-readable format.
 *
 *
 * @author clayjay3 (claytonraycowen@gmail.com)
 * @date 2025-08-23
 ******************************************************************************/
class SwitchMonitor
{
public:
    /******************************************************************************
     * @brief Construct a new Switch Monitor object.
     *
     * @param szIP - The IP address of the switch to monitor.
     * @param szUser - The username for SSH authentication.
     * @param szPass - The password for SSH authentication.
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    SwitchMonitor(const std::string &szIP, const std::string &szUser, const std::string &szPass)
    {
        if (szIP.empty() || szUser.empty() || szPass.empty())
        {
            throw std::invalid_argument("IP, User, and Pass must be provided");
        }

        // Initialize member variables.
        m_szIP = szIP;
        m_szUser = szUser;
        m_szPass = szPass;
        sshSession = nullptr;
    }

    /******************************************************************************
     * @brief Destroy the Switch Monitor object.
     *
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    ~SwitchMonitor()
    {
        // Clean up SSH channel.
        if (sshChannel)
        {
            ssh_channel_send_eof(sshChannel);
            ssh_channel_close(sshChannel);
            ssh_channel_free(sshChannel);
        }
        // Clean up SSH session.
        if (sshSession)
        {
            ssh_disconnect(sshSession);
            ssh_free(sshSession);
        }
    }

    /******************************************************************************
     * @brief Connect to the switch via SSH.
     *
     * @return true - Successfully connected.
     * @return false - Failed to connect.
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    bool Connect()
    {
        sshSession = ssh_new();
        if (!sshSession)
            return false;

        ssh_options_set(sshSession, SSH_OPTIONS_HOST, m_szIP.c_str());
        ssh_options_set(sshSession, SSH_OPTIONS_USER, m_szUser.c_str());
        ssh_options_set(sshSession, SSH_OPTIONS_HOSTKEYS, "ssh-rsa");

        if (ssh_connect(sshSession) != SSH_OK)
        {
            std::cerr << "Error connecting: " << ssh_get_error(sshSession) << "\n";
            return false;
        }

        if (ssh_userauth_password(sshSession, m_szUser.c_str(), m_szPass.c_str()) != SSH_AUTH_SUCCESS)
        {
            std::cerr << "Auth failed: " << ssh_get_error(sshSession) << "\n";
            return false;
        }

        // Open interactive channel with PTY + shell
        sshChannel = ssh_channel_new(sshSession);
        if (!sshChannel)
        {
            std::cerr << "Failed to create channel\n";
            return false;
        }
        if (ssh_channel_open_session(sshChannel) != SSH_OK)
        {
            std::cerr << "Failed to open channel: " << ssh_get_error(sshSession) << "\n";
            return false;
        }
        if (ssh_channel_request_pty(sshChannel) != SSH_OK)
        {
            std::cerr << "Failed to request PTY: " << ssh_get_error(sshSession) << "\n";
            return false;
        }
        if (ssh_channel_request_shell(sshChannel) != SSH_OK)
        {
            std::cerr << "Failed to request shell: " << ssh_get_error(sshSession) << "\n";
            return false;
        }

        this->RunCommand("terminal length 0");

        return true;
    }

    /******************************************************************************
     * @brief Run a command on the switch and return the szOutput.
     *
     * @param szCommand - The command to execute.
     * @return std::string - The command szOutput.
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    std::string RunCommand(const std::string &szCommand)
    {
        if (!sshChannel)
            throw std::runtime_error("SSH channel not initialized");

        // Send command (newline required)
        std::string fullCmd = szCommand + "\n";
        if (ssh_channel_write(sshChannel, fullCmd.c_str(), fullCmd.size()) < 0)
        {
            throw std::runtime_error("Failed to send command");
        }

        std::string szOutput;
        char buffer[512];
        int nbytes;
        bool sawPrompt = false;

        while (true)
        {
            nbytes = ssh_channel_read_nonblocking(sshChannel, buffer, sizeof(buffer), 0);
            if (nbytes > 0)
            {
                szOutput.append(buffer, nbytes);

                // Cisco prompts usually end with > or # followed by a space/newline
                if (szOutput.find("MRDT-CS-Rover#") != std::string::npos ||
                    szOutput.find("MRDT-CS-Rover>") != std::string::npos)
                {
                    // Found prompt, mark it
                    sawPrompt = true;

                    // Give device a tiny grace period for trailing output
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));

                    // Try one last drain
                    while ((nbytes = ssh_channel_read_nonblocking(sshChannel, buffer, sizeof(buffer), 0)) > 0)
                    {
                        szOutput.append(buffer, nbytes);
                    }

                    break;
                }
            }
            else
            {
                // No new data yet, wait briefly
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        // Strip the command echo and prompt from output
        auto pos = szOutput.find(szCommand);
        if (pos != std::string::npos)
        {
            szOutput = szOutput.substr(pos + szCommand.size());
        }
        // Trim trailing prompt
        size_t promptPos = szOutput.rfind("MRDT-CS-Rover#");
        if (promptPos != std::string::npos)
        {
            szOutput.erase(promptPos);
        }

        return szOutput;
    }

    /******************************************************************************
     * @brief Collect and parse statistics from the switch.
     *
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    void CollectStats()
    {
        std::string eigrp = RunCommand("show ip eigrp topology");
        std::string interfaces = RunCommand("show interfaces");

        std::cout << interfaces << std::endl;

        ParseEIGRP(eigrp);
        ParseInterfaces(interfaces);
    }

private:
    /******************************************************************************
     * @brief Parse EIGRP topology szData and display routes and their feasible distances.
     *
     * @param szData - The szData from "show ip eigrp topology" command.
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    void ParseEIGRP(const std::string &szData)
    {
        std::cout << "\n=== EIGRP Topology ===\n";
        std::regex re(R"((\d+\.\d+\.\d+\.\d+), Successor, FD is (\d+))");
        for (std::sregex_iterator i = std::sregex_iterator(szData.begin(), szData.end(), re);
             i != std::sregex_iterator(); ++i)
        {
            std::cout << "Route: " << (*i)[1] << " | FD: " << (*i)[2] << "\n";
        }
    }

    /******************************************************************************
     * @brief Parse interface statistics szData and display status, input/output packets and bytes.
     *
     * @param szData - The szData from "show interfaces" command.
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    void ParseInterfaces(const std::string &szData)
    {
        std::cout << "\n=== Interface Stats ===\n";
        std::regex re(R"(line protocol is (\w+).+?(\d+) packets input, (\d+) bytes.+?(\d+) packets szOutput, (\d+) bytes)");
        for (std::sregex_iterator i = std::sregex_iterator(szData.begin(), szData.end(), re);
             i != std::sregex_iterator(); ++i)
        {
            std::cout << "Status: " << (*i)[1]
                      << " | In: " << (*i)[2] << " pkts, " << (*i)[3] << " bytes"
                      << " | Out: " << (*i)[4] << " pkts, " << (*i)[5] << " bytes\n";
        }
    }

    // Member variables.
    std::string m_szIP, m_szUser, m_szPass;
    ssh_session sshSession;
    ssh_channel sshChannel;
};

/******************************************************************************
 * @brief Main function to initialize the SwitchMonitor, connect to the switch,
 *
 * @return int - Exit code.
 *
 * @author clayjay3 (claytonraycowen@gmail.com)
 * @date 2025-08-23
 ******************************************************************************/
int main()
{
    // Switch connection details.
    std::string szIP = "192.168.254.1";
    std::string szUser = "admin";
    std::string szPass = "nandgate";

    // Initialize and connect the monitor.
    SwitchMonitor Monitor(szIP, szUser, szPass);
    if (!Monitor.Connect())
    {
        std::cerr << "Failed to connect\n";
        return 1;
    }

    std::cout << "SHOW VERSION:" << std::endl;
    std::cout << Monitor.RunCommand("show version") << std::endl;
    std::cout << "SHOW IP EIGRP TOPOLOGY:" << std::endl;
    std::cout << Monitor.RunCommand("show ip eigrp topology") << std::endl;
    std::cout << "SHOW IP INT BR:" << std::endl;
    std::cout << Monitor.RunCommand("show ip int br") << std::endl;
    std::cout << "SHOW INT STATUS:" << std::endl;
    std::cout << Monitor.RunCommand("show int status") << std::endl;
    std::cout << "SHOW INTERFACES:" << std::endl;
    std::cout << Monitor.RunCommand("show interfaces") << std::endl;

    // // Periodically collect and display stats.
    // while (true)
    // {
    //     std::cout << "\n\n================= NEW SAMPLE =================\n";
    //     try
    //     {
    //         Monitor.CollectStats();
    //     }
    //     catch (const std::exception &e)
    //     {
    //         std::cerr << "Error collecting stats: " << e.what() << "\n";
    //     }

    //     // Wait before the next sample.
    //     std::this_thread::sleep_for(std::chrono::seconds(1));
    // }

    return 0;
}
