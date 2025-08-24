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
        // Initialize SSH session.
        sshSession = ssh_new();
        if (!sshSession)
        {
            return false;
        }

        // Set SSH options.
        ssh_options_set(sshSession, SSH_OPTIONS_HOST, m_szIP.c_str());
        ssh_options_set(sshSession, SSH_OPTIONS_USER, m_szUser.c_str());

        // Connect and authenticate.
        if (ssh_connect(sshSession) != SSH_OK)
        {
            std::cerr << "Error connecting: " << ssh_get_error(sshSession) << "\n";
            return false;
        }

        // Authenticate with password.
        if (ssh_userauth_password(sshSession, m_szUser.c_str(), m_szPass.c_str()) != SSH_AUTH_SUCCESS)
        {
            std::cerr << "Auth failed: " << ssh_get_error(sshSession) << "\n";
            return false;
        }

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
        // Create and open a new SSH channel.
        ssh_channel channel = ssh_channel_new(sshSession);
        // Check for channel creation errors.
        if (!channel)
        {
            throw std::runtime_error("Channel creation failed");
        }
        if (ssh_channel_open_session(channel) != SSH_OK)
        {
            throw std::runtime_error("Failed to open channel");
        }
        if (ssh_channel_request_exec(channel, szCommand.c_str()) != SSH_OK)
        {
            throw std::runtime_error("Exec request failed");
        }

        // Read command szOutput.
        char arBuffer[256];
        int nbytes;
        std::string szOutput;

        // Read until no more szData is available.
        while ((nbytes = ssh_channel_read(channel, arBuffer, sizeof(arBuffer), 0)) > 0)
        {
            szOutput.append(arBuffer, nbytes);
        }

        // Check for read errors.
        ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);

        // Return the command szOutput.
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
        std::string pingtest = RunCommand("ping 8.8.8.8 repeat 5");

        ParseEIGRP(eigrp);
        ParseInterfaces(interfaces);
        ParsePing(pingtest);
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

    /******************************************************************************
     * @brief Parse ping test szData and display the results.
     *
     * @param szData - The szData from "ping" command.
     *
     * @author clayjay3 (claytonraycowen@gmail.com)
     * @date 2025-08-23
     ******************************************************************************/
    void ParsePing(const std::string &szData)
    {
        std::cout << "\n=== Ping Test ===\n"
                  << szData << "\n";
    }

    // Member variables.
    std::string m_szIP, m_szUser, m_szPass;
    ssh_session sshSession;
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
    std::string szIP = "192.168.1.1";
    std::string szUser = "admin";
    std::string szPass = "password";

    // Initialize and connect the monitor.
    SwitchMonitor Monitor(szIP, szUser, szPass);
    if (!Monitor.Connect())
    {
        std::cerr << "Failed to connect\n";
        return 1;
    }

    // Periodically collect and display stats.
    while (true)
    {
        std::cout << "\n\n================= NEW SAMPLE =================\n";
        try
        {
            Monitor.CollectStats();
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error collecting stats: " << e.what() << "\n";
        }

        // Wait before the next sample.
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
