#include <cocos2d.h>
#include <Geode/modify/CCHttpClient.hpp>
#include <fmt/chrono.h>
#include <ghc/filesystem.hpp>
#include <fmt/format.h>
#include <fmt/color.h>

using namespace cocos2d;
using namespace cocos2d::extension;
namespace fs = ghc::filesystem;



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


struct RequestFields
{
	std::string_view url;
	int status;
	std::string postBody;
	std::string_view response;

private:
	static void logFieldConsole(RequestFieldEnum responseField, const auto& field)
	{
		auto paramStyle = fmt::fg(static_cast<fmt::color>(0x3b78ff));
		geode::log::info("{}: {}", fmt::styled(RequestFieldToString(responseField), paramStyle), field);
	}
public:
	void logConsole()
	{
		logFieldConsole(RequestFieldEnum::Url, url);
		logFieldConsole(RequestFieldEnum::StatusCode, status);
		logFieldConsole(RequestFieldEnum::RequestBody, postBody);
		logFieldConsole(RequestFieldEnum::Response, response);
	}

	void makeFileNameUnique(fs::path& filepath)
	{
		if(!fs::exists(filepath) || !filepath.has_filename()) return;


		std::string filename = filepath.stem().string();
		for(int i = 1; fs::exists(filepath); i++)
		{
			if(i > 100) return;
			filepath.replace_filename(fmt::format("{} #{}.log", filename, i));
		}
	}
	
	fs::path getFileNameFromUrl()
	{
		auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
		std::string timestr = fmt::format("{:%H-%M-%S}", fmt::localtime(now));
		fs::path savepath = geode::dirs::getGeodeLogDir();

		if(url.substr(url.size() - 4) == ".php")
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
		logFieldNoNewline(RequestFieldEnum::Response, response);
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

		auto requestFields = RequestFields {
			.url = httpRequest->getUrl(),
			.status = resp->getResponseCode(),
			.postBody = std::string(httpRequest->getRequestData(), httpRequest->getRequestDataSize()),
			.response = getResponseView(resp)
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