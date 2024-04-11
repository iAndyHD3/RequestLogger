#include <cocos2d.h>
#include <Geode/modify/CCHttpClient.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <fmt/chrono.h>
#include <ghc/filesystem.hpp>
#include <fmt/format.h>
#include <fmt/color.h>

using namespace cocos2d;
using namespace cocos2d::extension;
namespace fs = ghc::filesystem;



struct MenuLayerExt : geode::Modify<MenuLayerExt, MenuLayer>
{
    void onMoreGames(CCObject*) 
    {
        gout::info << "hello" << " " << "World" << "!";
    }
};



struct ResponseCallback
{
    CCObject* self;
    SEL_HttpResponse callback;
};

//adding it as a member doesnt work
std::unordered_map<CCHttpRequest*, ResponseCallback> originals;
std::string_view getResponseView(CCHttpResponse* r)
{
    auto* data = r->getResponseData();
    auto begin = data->begin();

    return {begin, data->end()};
}

enum class RequestFieldEnum
{
    Url,
    StatusCode,
    Response,
    RequestBody
};


std::string_view RequestFieldToString(RequestFieldEnum r)
{
    switch(r)
    {
        case RequestFieldEnum::Url: return "Url";
        case RequestFieldEnum::StatusCode: return "Status";
        case RequestFieldEnum::Response: return "Response";
        case RequestFieldEnum::RequestBody: return "Request Body";
        default: return "Unknown";
    }
}



std::vector<std::string_view> splitByDelimStringView(std::string_view str, char delim)
{
    std::vector<std::string_view> tokens;
    size_t pos = 0;
    size_t len = str.length();

    while (pos < len)
    {
        size_t end = str.find(delim, pos);
        if (end == std::string_view::npos)
        {
            tokens.emplace_back(str.substr(pos));
            break;
        }
        tokens.emplace_back(str.substr(pos, end - pos));
        pos = end + 1;
    }

    return tokens;
}

struct RequestFields
{
    std::string_view url;
    int status;
    std::string postBody;
    std::string_view response;
    std::string_view dataHeaders;

private:
    static void logFieldConsole(RequestFieldEnum responseField, const auto& field)
    {
        auto paramStyle = fmt::fg(static_cast<fmt::color>(0x3b78ff));
        geode::log::info("{}: {}", fmt::styled(RequestFieldToString(responseField), paramStyle), field);
    }
public:

    std::string_view getLogResponse()
    {
        for(const auto& header : splitByDelimStringView(dataHeaders, '\n'))
        {
            if(header.find("Content-Type: text/html") != std::string_view::npos)
            {
                return response;
            }
        }
        return "binary data";
    }

    std::string getColoredBodyParam(std::string_view param)
    {
        auto equal = param.find('=');
        if(equal == std::string_view::npos) [[unlikely]] return {};
        
        auto rgbToColor = [](const cocos2d::ccColor3B& c) { return (c.r << 16) | (c.g << 8) | c.b; };


        auto nameColor = geode::Mod::get()->getSettingValue<cocos2d::ccColor3B>("key-color");
        auto nameStyle = fmt::fg(static_cast<fmt::color>(rgbToColor(nameColor)));

        auto valueColor = geode::Mod::get()->getSettingValue<cocos2d::ccColor3B>("value-color");
        auto valueStyle = fmt::fg(static_cast<fmt::color>(rgbToColor(valueColor)));


        return fmt::format("{}{}",
            fmt::styled(param.substr(0, equal + 1), nameStyle),
            fmt::styled(param.substr(equal + 1), valueStyle)
        );
    }

    std::string getColoredBody()
    {
        std::vector<std::string> params;
        size_t finalStringSize = 0;
        for(const auto& param : splitByDelimStringView(postBody, '&'))
        {
            finalStringSize += params.emplace_back(getColoredBodyParam(param)).size() + 1;
        }
        std::string ret;
        ret.reserve(finalStringSize);
        for(const auto& paramStyled : params)
        {
            ret += paramStyled;
            ret += '&';
        }

        if(ret.empty()) [[unlikely]] return {};

        ret.pop_back();
        return ret;
    }

    void logConsole()
    {
        logFieldConsole(RequestFieldEnum::Url, url);
        logFieldConsole(RequestFieldEnum::StatusCode, status);

        bool color = geode::Mod::get()->getSettingValue<bool>("enable-color");
        logFieldConsole(RequestFieldEnum::RequestBody, color ? getColoredBody() : this->postBody);

        logFieldConsole(RequestFieldEnum::Response, getLogResponse());
    }

    void makeFileNameUnique(fs::path& filepath)
    {
        if(!fs::exists(filepath) || !filepath.has_filename()) return;


        std::string filename = filepath.stem().string();
        for(int i = 1; fs::exists(filepath); i++)
        {
            if(i > 100) [[unlikely]] return;
            filepath.replace_filename(fmt::format("{} #{}.log", filename, i));
        }
    }
    
    fs::path getFileNameFromUrl()
    {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::string timestr = fmt::format("{:%H-%M-%S}", fmt::localtime(now));
        fs::path savepath = geode::dirs::getGeodeLogDir();

        if(url.substr(url.size() - 4) == ".php") [[likely]]
        {
            auto slash = url.find_last_of('/');
            std::string_view fileurl = url.substr(slash + 1, (url.size() - 5) - slash);
            savepath /= fmt::format("{} {}.log", fileurl, timestr);
        }
        else
        {
            savepath /= fmt::format("Request {}.log", timestr);
        }

        makeFileNameUnique(savepath);
        return savepath;
    }

    void logFile()
    {
        fs::path path = getFileNameFromUrl();
        std::ofstream logfile(path);
        if(!logfile.good()) return geode::log::error("Could not log to {}", path);

        auto logField = [&](RequestFieldEnum f, const auto& field)
        {
            logfile << fmt::format("{}:\n{}\n", RequestFieldToString(f), field);
        };
        auto logFieldNoNewline = [&](RequestFieldEnum f, const auto& field)
        {
            logfile << fmt::format("{}:\n{}", RequestFieldToString(f), field);
        };

        logField(RequestFieldEnum::Url, url);
        logField(RequestFieldEnum::StatusCode, status);
        logField(RequestFieldEnum::RequestBody, postBody);
        logFieldNoNewline(RequestFieldEnum::Response, getLogResponse());
    }

    bool shouldLogConsole() { return geode::Mod::get()->getSettingValue<bool>("log-to-console"); }
    bool shouldLogFile() { return geode::Mod::get()->getSettingValue<bool>("log-to-file"); }
};

struct HttpLogger : public geode::Modify<HttpLogger, CCHttpClient>
{
    void onResp(cocos2d::extension::CCHttpClient *client, cocos2d::extension::CCHttpResponse *resp)
    {
        auto httpRequest = resp->getHttpRequest();
        
        auto it = originals.find(httpRequest);
        if(it == originals.end()) return;

        auto data = resp->getResponseHeader();

        auto requestFields = RequestFields {
            .url = httpRequest->getUrl(),
            .status = resp->getResponseCode(),
            .postBody = std::string(httpRequest->getRequestData(), httpRequest->getRequestDataSize()),
            .response = getResponseView(resp),
            .dataHeaders = {data->begin(), data->end()}
        };

        if(requestFields.shouldLogConsole()) requestFields.logConsole();
        if(requestFields.shouldLogFile()) requestFields.logFile();

        //https://github.com/cocos2d/cocos2d-x/blob/cocos2d-x-2.2.3/extensions/network/HttpClient.cpp#L498
        CCObject* self = (*it).second.self;
        SEL_HttpResponse callback = (*it).second.callback;
        (self->*callback)(client, resp);

        originals.erase(it);
    }

    void send(CCHttpRequest* req)
    {
        //if both disabled do nothing
        auto mod = geode::Mod::get();
        bool console = mod->getSettingValue<bool>("log-to-console");
        bool file = mod->getSettingValue<bool>("log-to-file");
        if(!file && !console) return CCHttpClient::send(req);

        originals.insert({req, {.self = req->getTarget(), .callback = req->getSelector()}});
        req->setResponseCallback(this, httpresponse_selector(HttpLogger::onResp));
        CCHttpClient::send(req);
    }
};
