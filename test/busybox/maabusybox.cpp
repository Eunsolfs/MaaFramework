#define _CRT_SECURE_NO_WARNINGS

#include "Controller/Platform/PlatformFactory.h"
#include "Controller/Unit/ControlUnit.h"
#include "MaaAPI.h"
#include "Utils/ArgvWrapper.hpp"
#include "Utils/NoWarningCV.h"
#include "Utils/StringMisc.hpp"
#include "cxxopts.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>

std::ostream& operator<<(std::ostream& os, const MaaNS::ControllerNS::Unit::DeviceInfo::Resolution& res)
{
    return os << "{ width: " << res.width << ", height: " << res.height << " }";
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::vector<T>& v)
{
    os << "[";
    std::copy(v.begin(), v.end(), std::ostream_iterator<T>(os, ", "));
    return os << "]";
}

template <typename T>
std::ostream& operator<<(std::ostream& os, const std::optional<T>& v)
{
    if (v.has_value()) {
        return os << v.value();
    }
    else {
        return os << "<nullopt>";
    }
}

inline std::string read_controller_config(const std::string& cur_dir)
{
    std::ifstream ifs(std::filesystem::path(cur_dir) / "config" / "controller_config.json", std::ios::in);
    if (!ifs.is_open()) {
        std::cerr << "Failed to open controller_config.json\n"
                  << "Please copy controller_config.json to " << std::filesystem::path(cur_dir) / "config" << std::endl;
        exit(1);
    }

    std::stringstream buffer;
    buffer << ifs.rdbuf();
    return buffer.str();
}

// 用string防止后续的replacement触发explicit构造函数
std::map<std::string_view, std::string> intents = {
    { "Official", "com.hypergryph.arknights/com.u8.sdk.U8UnityContext" },
    { "Bilibili", "com.hypergryph.arknights.bilibili/com.u8.sdk.U8UnityContext" },
    { "YoStarEN", "com.YoStarEN.Arknights/com.u8.sdk.U8UnityContext" },
    { "YoStarJP", "com.YoStarJP.Arknights/com.u8.sdk.U8UnityContext" },
    { "YoStarKR", "com.YoStarKR.Arknights/com.u8.sdk.U8UnityContext" },
    { "txwy", "tw.txwy.and.arknights/com.u8.sdk.U8UnityContext" }
};

template <typename SCP>
double test_screencap(SCP* scp, int count = 10)
{
    std::chrono::milliseconds sum(0);
    for (int i = 0; i < count; i++) {
        auto now = std::chrono::steady_clock::now();
        auto mat = scp->screencap();
        if (mat.has_value()) {
            auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - now);

            auto file = std::format("temp-{}.png", i);
            cv::imwrite(file, mat.value());
            std::cout << "image saved to " << file << std::endl;

            std::cout << "time cost: " << dur << std::endl;
            sum += dur;
        }
    }
    double cost = sum.count() / double(count);
    std::cout << "average time cost: " << cost << "ms" << std::endl;
    return cost;
}

int main(int argc, char* argv[])
{
    cxxopts::Options options(argv[0], "Maa utility tool for test purpose.");

    std::string adb = "adb";
    std::string adb_address = "127.0.0.1:5555";
    std::string client = "Official";

    if (getenv("MAA_ADB")) {
        adb = getenv("MAA_ADB");
    }

    if (getenv("MAA_ADB_SERIAL")) {
        adb_address = getenv("MAA_ADB_SERIAL");
    }

    if (getenv("MAA_CLIENT")) {
        client = getenv("MAA_CLIENT");
    }

    // clang-format off
    options.add_options()
        ("a,adb", "adb path, $MAA_ADB", cxxopts::value<std::string>()->default_value(adb))
        ("s,serial", "adb address, $MAA_ADB_SERIAL", cxxopts::value<std::string>()->default_value(adb_address))
        ("c,config", "config directory", cxxopts::value<std::string>()->default_value((std::filesystem::current_path()).string()))
        ("t,client", "client, $MAA_CLIENT", cxxopts::value<std::string>()->default_value(client))
        ("h,help", "print usage", cxxopts::value<bool>())

        ("command", "command", cxxopts::value<std::string>()->default_value("help"))
        ("subcommand", "sub command", cxxopts::value<std::string>()->default_value("help"))
        ("params", "rest params", cxxopts::value<std::vector<std::string>>()->default_value(""))
        ;
    // clang-format on

    options.parse_positional({ "command", "subcommand", "params" });

    options.positional_help("[COMMAND] [SUBCOMMAND]");

    auto result = options.parse(argc, argv);

    auto cmd = result["command"].as<std::string>();

    if (cmd == "help" || result["help"].as<bool>()) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    using namespace MaaNS::ControllerNS;

    auto config = json::parse(read_controller_config(result["config"].as<std::string>()));
    auto io = PlatformFactory::create();
    MaaSetGlobalOption(MaaGlobalOption_Logging, (std::filesystem::current_path() / "debug").string().c_str());

    Unit::UnitBase::Argv::replacement adbRepl = { { "{ADB}", adb }, { "{ADB_SERIAL}", adb_address } };

    auto initUnit = [io, &config, &adbRepl]<typename Unit>(Unit* unit) -> Unit* {
        unit->set_io(io);
        unit->parse(config.value());
        unit->set_replacement(adbRepl);
        return unit;
    };

    if (cmd == "connect") {
        auto connect = initUnit(new Unit::Connection);

        std::cout << "return: " << std::boolalpha << connect->connect() << std::endl;
    }
    else if (cmd == "device_info") {
        auto device = initUnit(new Unit::DeviceInfo);

        std::cout << "uuid: " << device->request_uuid() << std::endl;
        std::cout << "resolution: " << device->request_resolution() << std::endl;
        std::cout << "orientation: " << device->request_orientation() << std::endl;
    }
    else if (cmd == "activity") {
        auto activity = initUnit(new Unit::Activity);

        auto scmd = result["subcommand"].as<std::string>();

        if (scmd == "help") {
            std::cout << "Usage: " << argv[0] << " activity [start | stop]" << std::endl;
        }
        else if (scmd == "start") {
            std::cout << "return: " << std::boolalpha << activity->start(intents[client]) << std::endl;
        }
        else if (scmd == "stop") {
            std::cout << "return: " << std::boolalpha << activity->stop(intents[client]) << std::endl;
        }
    }
    // else if (cmd == "tap_input") {
    //     auto tap = initUnit(new Unit::TapInput);

    //     auto scmd = result["subcommand"].as<std::string>();
    //     auto params = result["params"].as<std::vector<std::string>>();

    //     if (scmd == "help") {
    //         std::cout << "Usage: " << argv[0] << " tap_input [click | swipe | press_key]" << std::endl;
    //     }
    //     else if (scmd == "click") {
    //         if (params.size() < 2) {
    //             std::cout << "Usage: " << argv[0] << " tap_input click [X] [Y]" << std::endl;
    //             return 0;
    //         }

    //         int x = atoi(params[0].c_str());
    //         int y = atoi(params[1].c_str());
    //         std::cout << "return: " << std::boolalpha << tap->click(x, y) << std::endl;
    //     }
    //     else if (scmd == "swipe") {
    //         if (params.size() < 5) {
    //             std::cout << "Usage: " << argv[0] << " tap_input swipe [X1] [Y1] [X2] [Y2] [DURATION]" << std::endl;
    //             return 0;
    //         }

    //         int x1 = atoi(params[0].c_str());
    //         int y1 = atoi(params[1].c_str());
    //         int x2 = atoi(params[2].c_str());
    //         int y2 = atoi(params[3].c_str());
    //         int dur = atoi(params[4].c_str());
    //         std::cout << "return: " << std::boolalpha << tap->swipe(x1, y1, x2, y2, dur) << std::endl;
    //     }
    //     else if (scmd == "press_key") {
    //         if (params.size() < 1) {
    //             std::cout << "Usage: " << argv[0] << " tap_input press_key [KEY]" << std::endl;
    //             return 0;
    //         }

    //         int key = atoi(params[0].c_str());
    //         std::cout << "return: " << std::boolalpha << tap->press_key(key) << std::endl;
    //     }
    // }
    else if (cmd == "screencap") {
        auto device = initUnit(new Unit::DeviceInfo);

        auto res = device->request_resolution();

        auto scmd = result["subcommand"].as<std::string>();
        // auto params = result["params"].as<std::vector<std::string>>();

        if (scmd == "help") {
            std::cout << "Usage: " << argv[0]
                      << " screencap [profile | raw_by_netcat | raw_with_gzip | encode | encode_to_file | "
                         "minicap_direct | minicap_strean]"
                      << std::endl;
            return 0;
        }

        bool profile = false;
        std::map<std::string, double> cost;

        auto scp = initUnit(new Unit::Screencap);
        scp->init(res.value().width, res.value().height);

        if (scmd == "profile") {
            profile = true;
        }

#define TEST_SC(method, methodEnum)                                                                           \
    if (profile || scmd == #method) {                                                                         \
        cost[#method] = test_screencap(scp->get_unit(MAA_CTRL_UNIT_NS::Screencap::Method::methodEnum).get()); \
    }

        TEST_SC(raw_by_netcat, RawByNetcat)
        TEST_SC(raw_with_gzip, RawWithGzip)
        TEST_SC(encode, Encode)
        TEST_SC(encode_to_file, EncodeToFileAndPull)
        TEST_SC(minicap_direct, MinicapDirect)
        TEST_SC(minicap_stream, MinicapStream)

        if (profile) {
            std::cout << "\n\nResult: " << std::endl;
            for (const auto& pr : cost) {
                std::cout << pr.first << ": " << pr.second << "ms" << std::endl;
            }
            std::cout << "\n" << std::endl;
        }
    }
    // else if (cmd == "invoke_app") {
    //     auto inv = initUnit(new Unit::InvokeApp);

    //     std::ifstream f(".invokeapp");
    //     std::string invtp = "";
    //     if (f.is_open()) {
    //         f >> invtp;
    //         f.close();
    //     }

    //     inv->init(invtp);

    //     invtp = inv->get_tempname();

    //     auto scmd = result["subcommand"].as<std::string>();
    //     auto params = result["params"].as<std::vector<std::string>>();

    //     auto trackMinitouch = [](std::shared_ptr<MaaNS::ControllerNS::IOHandler> h) {
    //         while (true) {
    //             std::cout << "reading info..." << std::endl;
    //             auto str = h->read(2);
    //             if (str.empty()) {
    //                 std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    //                 continue;
    //             }
    //             auto pos = str.find('^');
    //             if (pos == std::string::npos) {
    //                 continue;
    //             }
    //             auto rpos = str.find('\n', pos);
    //             if (rpos == std::string::npos) {
    //                 continue;
    //             }
    //             auto info = str.substr(pos + 1, rpos - pos - 1);
    //             std::cout << "minitouch info: " << info << std::endl;
    //             break;
    //         }
    //         while (true) {
    //             std::string row;
    //             std::getline(std::cin, row);
    //             h->write(row + '\n');
    //         }
    //     };

    //     if (scmd == "help") {
    //         std::cout << "Usage: " << argv[0] << " invoke_app [abilist | push | chmod | invoke_bin]" << std::endl;
    //     }
    //     else if (scmd == "abilist") {
    //         std::cout << inv->abilist() << std::endl;
    //     }
    //     else if (scmd == "push") {
    //         if (params.size() < 1) {
    //             std::cout << "Usage: " << argv[0] << " invoke_app push [file]" << std::endl;
    //             return 0;
    //         }

    //         std::cout << "push as " << invtp << std::endl;
    //         std::cout << "return: " << std::boolalpha << inv->push(params[0]) << std::endl;
    //         std::ofstream fo(".invokeapp");
    //         fo << inv->get_tempname();
    //     }
    //     else if (scmd == "chmod") {
    //         std::cout << "chmod of " << invtp << std::endl;
    //         std::cout << "return: " << std::boolalpha << inv->chmod() << std::endl;
    //     }
    //     else if (scmd == "invoke_bin") {
    //         while (params.size() > 0 && params[0].empty()) {
    //             params.erase(params.begin());
    //         }
    //         std::cout << "params: " << params << std::endl;
    //         auto h = inv->invoke_bin(params.size() > 0 ? params[0] : "");
    //         trackMinitouch(h);
    //     }
    //     else if (scmd == "invoke_app") {
    //         if (params.size() < 1) {
    //             std::cout << "Usage: " << argv[0] << " invoke_app invoke_app [package]" << std::endl;
    //             return 0;
    //         }

    //         auto h = inv->invoke_app(params[0]);
    //         trackMinitouch(h);
    //     }
    // }
}