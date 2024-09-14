#ifndef _CAN_GEN_H_
#define _CAN_GEN_H_

#include <string>
#include <yaml-cpp/yaml.h>
#include <map>
#include <boost/asio.hpp>
#include <algorithm>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <unistd.h>
#include <CanDriver.hpp>
#include <memory>
#include <dbcppp/Network.h>
#include <fstream>
#include <optional>
#include <bitset>
#include <cstdint>


struct BaseConfig {
    std::string interfaceName;
    std::string dbcFilePath;
    std::string customWaveFilePath;
};

enum class TransistionTyp : uint32_t{
    staticTransistion,
    linearTransistion
};

struct Wave {
    std::map<int, double> multiWaveSteps;
    double value;
    double m;
    TransistionTyp transistionTyp;
};

struct WaveSignalConfig {
    int noise;
    Wave wave;

};

struct WaveMessageConfig {
    //Signalname - WaveSignalConfig
    std::map<std::string, WaveSignalConfig> signalConfig;
};

struct WaveConfig {
    //Messagename - WaveMessageConfig
    std::map<std::string, WaveMessageConfig> messages;
};





class CanGen {
    public:
        CanGen(){}



        static bool importBaseConfig(const std::string& baseConfigFilePath) {
            try {
                YAML::Node config = YAML::LoadFile(baseConfigFilePath);

                auto root = config["BaseConfig"];

                for (const auto& subNode : root) {
                    BaseConfig baseConfig;

                    baseConfig.interfaceName = subNode["interfaceName"].as<std::string>();
                    baseConfig.dbcFilePath = subNode["dbcFileName"].as<std::string>();
                    baseConfig.customWaveFilePath = subNode["customWaveFile"].as<std::string>();
                    
                    m_interfaceConfigMap.emplace(baseConfig.interfaceName, std::move(baseConfig));
                }

            }catch(const YAML::ParserException& ex){
                std::cerr << "Yaml File could not be parsed" << std::endl;
                std::cerr << "Filename: " << baseConfigFilePath << std::endl;
                return false;
            }

            return true;
        }

        static std::optional<const dbcppp::IMessage*> getDbcMessageFromMessageName(const std::unordered_map<uint64_t, const dbcppp::IMessage*>& dbcMessages, const std::string& messageName) {
            for (const auto& [id, message] : dbcMessages) {
                if(message->Name() == messageName) {
                    return message;
                }
            }
            return {};
        }

        static std::optional<const dbcppp::ISignal*> getDbcSignalFromSignalName(const dbcppp::IMessage* iMessage, const std::string& signalName) {
            for (const auto& sig : iMessage->Signals()) {
                if (sig.Name() == signalName) {
                    return &sig;
                }
            }
            return {};
        }

        static void processMessages() {

            static bool mHasCanged = false;

            for (auto& [interfaceName, waveConfig] : m_waveSingleConfigMap) {
                
                if (!m_interfaceCanMap.count(interfaceName)) {
                    return;
                }

                auto& canDriver = m_interfaceCanMap.at(interfaceName);

                for (auto& [messageName, messageConfig] : waveConfig.messages){

                    if(!m_dbcMessages.count(interfaceName)) {
                        std::cerr << "Cant find Message in dbcMessages" << std::endl;
                        std::cerr << "MessageName: " << interfaceName << std::endl;
                        break;
                    }

                    auto dbcMessageNet = m_dbcMessages.at(interfaceName);

                    auto dbcMessage = getDbcMessageFromMessageName(dbcMessageNet, messageName);
                    
                    if(!dbcMessage.has_value()) {
                        std::cerr << "Cant find Message Name in dbcMessage" << std::endl;
                        std::cerr << "MessageName: " << messageName << std::endl;
                        break;
                    }

                    auto dbcIMessage = dbcMessage.value();

                    can_frame frame;
                    
                    for (auto& [signalName, waveSignalConfig] : messageConfig.signalConfig) {

                        bool foundNewGlobalStep = false;
                        auto itMwsBegin = waveSignalConfig.wave.multiWaveSteps.begin();
                        auto itMwsEnd = waveSignalConfig.wave.multiWaveSteps.end();
                        for (; itMwsBegin != itMwsEnd; itMwsBegin++) {
                            if(itMwsBegin->first == m_globalStep) {
                                foundNewGlobalStep = true;
                                break;
                            }
                        }
                        if (foundNewGlobalStep) {
                            if(waveSignalConfig.wave.transistionTyp == TransistionTyp::staticTransistion) {
                                waveSignalConfig.wave.value = itMwsBegin->second;
                            }
                            else if(waveSignalConfig.wave.transistionTyp ==  TransistionTyp::linearTransistion) {
                                double y = itMwsBegin->second;
                                int x = itMwsBegin->first;
                                
                                itMwsBegin++;
                                if (itMwsBegin != itMwsEnd) {
                                    double deltaX = (double)(itMwsBegin->first - x);
                                    double deltaY = (double)(itMwsBegin->second - y);
                                    waveSignalConfig.wave.m = deltaY / deltaX;
                                    std::cout << "m = " << waveSignalConfig.wave.m << " - ";
                                    mHasCanged = true;
                                } else {
                                    waveSignalConfig.wave.transistionTyp = TransistionTyp::staticTransistion;
                                }

                                waveSignalConfig.wave.value = y;
                            }
                        } else {
                            if(waveSignalConfig.wave.transistionTyp ==  TransistionTyp::linearTransistion) {
                               waveSignalConfig.wave.value += waveSignalConfig.wave.m;
                               //std::cout << "f(" << m_globalStep << ") = " << waveSignalConfig.wave.value << std::endl;
                            }
                        }
 
                        auto dbcSignal = getDbcSignalFromSignalName(dbcIMessage, signalName);

                        if (!dbcSignal.has_value()) {
                            std::cerr << "Cant find Signal Name in IMessage" << std::endl;
                            std::cerr << "Signalname: " << signalName << std::endl;
                            return;
                        }

                        auto dbcISignal = dbcSignal.value();

                        auto raw = dbcISignal->PhysToRaw(waveSignalConfig.wave.value);
                        dbcISignal->Encode(raw, frame.data);

                    }
                    
                    frame.can_id = dbcIMessage->Id();
                    frame.len = dbcIMessage->MessageSize();

                    sockcanpp::CanMessage canMessage(frame);
                    auto sendSize = canDriver->sendMessage(canMessage);
                    
                    if (mHasCanged) {
                    std::cout << std::endl;
                    mHasCanged = false;
                }
                    
                }
                
            }
            m_globalStep++;
        }

        static void timerCallback(const boost::system::error_code&, boost::asio::steady_timer* timer) {
            processMessages();
            timer->expires_at(timer->expiry() + boost::asio::chrono::milliseconds(m_globalUpdateDuration));
            timer->async_wait(std::bind(timerCallback, std::placeholders::_1, timer));
        }

        static bool initCan(const std::string& interfaceName) {
            auto canDriver = std::make_unique<sockcanpp::CanDriver>(interfaceName, 1);
            m_interfaceCanMap.emplace(interfaceName, std::move(canDriver));
            return true;
        }

        static bool initDbc(const std::string& interfaceName, const std::string& dbcFilePath) {
            std::ifstream idbc(dbcFilePath);
            if(!idbc) {
                std::cerr << "Cant open .dbc File" << std::endl;
                std::cerr << "Filename: " << dbcFilePath << std::endl;
                return false;
            }

            std::unique_ptr<dbcppp::INetwork> net = dbcppp::INetwork::LoadDBCFromIs(idbc);

            std::unordered_map<uint64_t, const dbcppp::IMessage*> dbcMessages;
            for (const auto& msg : net->Messages()) {
                dbcMessages.emplace(msg.Id(), &msg);
            }

            m_dbcMessages.emplace(interfaceName, std::move(dbcMessages));
            m_interfaceDbcMap.emplace(interfaceName, std::move(net));

            return true;
        }

        static void init() {
            for (const auto&[interfaceName, baseConfig] : m_interfaceConfigMap) {
                if(!importWaveConfig(baseConfig.customWaveFilePath, interfaceName)) return;

                

                if(!initCan(interfaceName)) return;

                if(!initDbc(interfaceName, baseConfig.dbcFilePath)) return;
            }

            boost::asio::steady_timer timer(m_io, boost::asio::chrono::milliseconds(m_globalUpdateDuration));
            timer.async_wait(std::bind(timerCallback, std::placeholders::_1, &timer));

            //print();

            std::cout << "Started Timer" << std::endl;
            m_io.run();
        }


        static bool isNumber(const std::string& val) {
            
            for (const char c : val) {
                if(c < '0' || c > '9') return false;
            }
            
            return true;
        }

        static TransistionTyp getTransistionTyp(const std::string& transformType) {
            if(transformType == "linear") {
                return TransistionTyp::linearTransistion;
            } else {
                return TransistionTyp::staticTransistion;
            }
        }

        static bool importWaveConfig(const std::string& waveConfigFilePath, const std::string& interfaceName) {
            try {
                YAML::Node config = YAML::LoadFile(waveConfigFilePath);

                m_gloablUpdateDurationUnit = config["waveConfig"]["updateDuration"]["unit"].as<std::string>();
                m_globalUpdateDuration = config["waveConfig"]["updateDuration"]["duration"].as<int>();


                WaveConfig waveConfig;

                for (const auto& set : config["waveConfig"]["waveForms"]) {
                    std::string type = set["Set"]["Typ"].as<std::string>();

                    if(type == "single") {
                        std::string messageName = set["Set"]["message"]["messageName"].as<std::string>();
                        WaveMessageConfig waveMessageConfig;
                        for (const auto& messageSignals : set["Set"]["message"]["messageSignals"]) {
                            WaveSignalConfig waveSignalConfig;
                            std::string signalName = messageSignals["signalName"].as<std::string>();
                            int signalNoise = messageSignals["signalNoise"].as<int>();
                            waveSignalConfig.noise = signalNoise;
                            std::string transformType = messageSignals["transformType"].as<std::string>();
                            Wave s_wave;
                            s_wave.transistionTyp = getTransistionTyp(transformType);
                            for(const auto& wave : messageSignals["wave"]) {
                                std::string firstNum = wave.first.as<std::string>();
                                std::string secondNum  = wave.second.as<std::string>();
                                if(isNumber(firstNum) && isNumber(secondNum)) {
                                    s_wave.multiWaveSteps.emplace(atoi(firstNum.c_str()), static_cast<double>(atoi(secondNum.c_str())));
                                }
                            }
                            waveSignalConfig.wave = s_wave;
                            waveMessageConfig.signalConfig.emplace(signalName, std::move(waveSignalConfig));
                        }

                        waveConfig.messages.emplace(messageName, std::move(waveMessageConfig));
                    } 
                    else if(type == "multi") {

                        WaveSignalConfig waveSignalConfig;

                        int signalNoise = set["Set"]["message"]["partsConfig"]["signalNoise"].as<int>();
                        waveSignalConfig.noise = signalNoise;

                        Wave s_wave;
                        std::string transformType = set["Set"]["message"]["partsConfig"]["transformType"].as<std::string>();
                        s_wave.transistionTyp = getTransistionTyp(transformType);


                        for (const auto& wave : set["Set"]["message"]["partsConfig"]["wave"]) {
                            std::string firstNum = wave.first.as<std::string>();
                            std::string secondNum  = wave.second.as<std::string>();

                            if(isNumber(firstNum) && isNumber(secondNum)) {
                                s_wave.multiWaveSteps.emplace(atoi(firstNum.c_str()), static_cast<double>(atoi(secondNum.c_str())));
                            }
                        }

                        waveSignalConfig.wave = s_wave;


                        
                        for (const auto& parts : set["Set"]["message"]["parts"]) {
                            std::string messageName = parts["messageName"].as<std::string>();
                            WaveMessageConfig waveMessageConfig;
                            
                            for (const auto& messageSignals : parts["messageSignals"]) {
                                waveMessageConfig.signalConfig.emplace(messageSignals.as<std::string>(), waveSignalConfig);
                            }

                            waveConfig.messages.emplace(messageName, waveMessageConfig);
                            
                        }
                        
                    }
                    
                    

                }

                m_waveSingleConfigMap.emplace(interfaceName, std::move(waveConfig));

            } catch (const YAML::ParserException& ex) {
                std::cerr << "YAML-Datei konnte nicht geparst werden: " << ex.what() << std::endl;
            } catch (const std::exception& ex) {
                std::cerr << "Fehler: " << ex.what() << std::endl;
            }


            return true;
        }


        static void print() {
            for (const auto& [interfaceName, waveSingleConfig] : m_waveSingleConfigMap) {
                std::cout << "InterfaceName: " << interfaceName << std::endl;
                for (const auto& [messageName, waveMessageConfig] : waveSingleConfig.messages) {
                    std::cout << "\t" << "MessageName: " << messageName << std::endl;
                    for (const auto& [signalName, waveSignalConfig] : waveMessageConfig.signalConfig) {
                        std::cout << "\t\t" << "SignalName: " << signalName << std::endl;
                        std::cout << "\t\t\t" << "Noise: " << waveSignalConfig.noise << std::endl;
                        for (const auto& [count, value] : waveSignalConfig.wave.multiWaveSteps) {
                            std::cout << "\t\t\t\t" << "count(" << count << ") = " << value << std::endl; 
                        }
                    }
                }
            }
        }

    private:
        static std::map<std::string, BaseConfig> m_interfaceConfigMap;
        static std::map<std::string, WaveConfig> m_waveSingleConfigMap;


    public:
        static int m_globalStep;
        static int m_globalUpdateDuration;
        static std::string m_gloablUpdateDurationUnit;

    private:
        static boost::asio::io_context m_io;

    
    private:
        static std::map<std::string, std::unique_ptr<sockcanpp::CanDriver>> m_interfaceCanMap;

    private:
        static std::map<std::string, std::unique_ptr<dbcppp::INetwork>> m_interfaceDbcMap;
        static std::map<std::string, std::unordered_map<uint64_t, const dbcppp::IMessage*>> m_dbcMessages;

};

std::map<std::string, BaseConfig> CanGen::m_interfaceConfigMap{};
std::map<std::string, WaveConfig> CanGen::m_waveSingleConfigMap{};

int CanGen::m_globalUpdateDuration = 10;
int CanGen::m_globalStep = 0;

std::string CanGen::m_gloablUpdateDurationUnit{};
boost::asio::io_context CanGen::m_io{};

std::map<std::string, std::unique_ptr<sockcanpp::CanDriver>> CanGen::m_interfaceCanMap{};

std::map<std::string, std::unique_ptr<dbcppp::INetwork>> CanGen::m_interfaceDbcMap{};
std::map<std::string, std::unordered_map<uint64_t, const dbcppp::IMessage*>> CanGen::m_dbcMessages{};

#endif