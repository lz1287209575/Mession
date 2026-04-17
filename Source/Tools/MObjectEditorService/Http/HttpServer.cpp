#include "Tools/MObjectEditorService/Http/HttpServer.h"

#include "Common/IO/Socket/SocketPlatform.h"
#include "Common/Runtime/Json.h"
#include "Common/Runtime/Log/Logger.h"

#include <chrono>
#include <cctype>
#include <cstring>
#include <sstream>

namespace
{
constexpr size_t MaxHeaderBytes = 64 * 1024;
constexpr size_t MaxBodyBytes = 2 * 1024 * 1024;

MString ToLowerCopy(const MString& InValue)
{
    MString Result = InValue;
    for (char& Ch : Result)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Result;
}

MString TrimCopy(const MString& InValue)
{
    size_t Start = 0;
    while (Start < InValue.size() && std::isspace(static_cast<unsigned char>(InValue[Start])))
    {
        ++Start;
    }

    size_t End = InValue.size();
    while (End > Start && std::isspace(static_cast<unsigned char>(InValue[End - 1])))
    {
        --End;
    }

    return InValue.substr(Start, End - Start);
}

int DecodeHexNibble(char Ch)
{
    if (Ch >= '0' && Ch <= '9')
    {
        return Ch - '0';
    }
    if (Ch >= 'a' && Ch <= 'f')
    {
        return 10 + (Ch - 'a');
    }
    if (Ch >= 'A' && Ch <= 'F')
    {
        return 10 + (Ch - 'A');
    }
    return -1;
}

MString UrlDecode(const MString& InValue)
{
    MString Result;
    Result.reserve(InValue.size());

    for (size_t Index = 0; Index < InValue.size(); ++Index)
    {
        const char Ch = InValue[Index];
        if (Ch == '+')
        {
            Result.push_back(' ');
            continue;
        }

        if (Ch == '%' && Index + 2 < InValue.size())
        {
            const int High = DecodeHexNibble(InValue[Index + 1]);
            const int Low = DecodeHexNibble(InValue[Index + 2]);
            if (High >= 0 && Low >= 0)
            {
                Result.push_back(static_cast<char>((High << 4) | Low));
                Index += 2;
                continue;
            }
        }

        Result.push_back(Ch);
    }

    return Result;
}

MString StatusTextFromCode(int StatusCode)
{
    switch (StatusCode)
    {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 500: return "Internal Server Error";
    default: return "OK";
    }
}

void ParseQueryString(const MString& QueryString, TMap<MString, MString>& OutQueryParams)
{
    size_t Start = 0;
    while (Start <= QueryString.size())
    {
        const size_t Ampersand = QueryString.find('&', Start);
        const size_t End = (Ampersand == MString::npos) ? QueryString.size() : Ampersand;
        const MString Pair = QueryString.substr(Start, End - Start);
        if (!Pair.empty())
        {
            const size_t Equals = Pair.find('=');
            const MString Key = (Equals == MString::npos) ? Pair : Pair.substr(0, Equals);
            const MString Value = (Equals == MString::npos) ? MString() : Pair.substr(Equals + 1);
            OutQueryParams[UrlDecode(Key)] = UrlDecode(Value);
        }

        if (Ampersand == MString::npos)
        {
            break;
        }

        Start = Ampersand + 1;
    }
}

bool TryParseMethod(const MString& MethodToken, EEditorHttpMethod& OutMethod)
{
    if (MethodToken == "GET")
    {
        OutMethod = EEditorHttpMethod::Get;
        return true;
    }
    if (MethodToken == "POST")
    {
        OutMethod = EEditorHttpMethod::Post;
        return true;
    }
    if (MethodToken == "OPTIONS")
    {
        OutMethod = EEditorHttpMethod::Options;
        return true;
    }

    OutMethod = EEditorHttpMethod::Unknown;
    return false;
}

bool TryParseRequest(const MString& RawRequest, FEditorHttpRequest& OutRequest, MString& OutError)
{
    const size_t HeaderEnd = RawRequest.find("\r\n\r\n");
    if (HeaderEnd == MString::npos)
    {
        OutError = "http_request_header_incomplete";
        return false;
    }

    const size_t FirstLineEnd = RawRequest.find("\r\n");
    if (FirstLineEnd == MString::npos)
    {
        OutError = "http_request_line_missing";
        return false;
    }

    {
        std::istringstream Stream(RawRequest.substr(0, FirstLineEnd));
        MString MethodToken;
        MString RawTarget;
        MString Version;
        Stream >> MethodToken >> RawTarget >> Version;
        if (MethodToken.empty() || RawTarget.empty() || Version.empty())
        {
            OutError = "http_request_line_invalid";
            return false;
        }

        TryParseMethod(MethodToken, OutRequest.Method);
        OutRequest.RawTarget = RawTarget;
    }

    size_t LineStart = FirstLineEnd + 2;
    while (LineStart < HeaderEnd)
    {
        const size_t LineEnd = RawRequest.find("\r\n", LineStart);
        if (LineEnd == MString::npos || LineEnd > HeaderEnd)
        {
            break;
        }

        const MString HeaderLine = RawRequest.substr(LineStart, LineEnd - LineStart);
        const size_t Colon = HeaderLine.find(':');
        if (Colon == MString::npos)
        {
            OutError = "http_header_invalid";
            return false;
        }

        const MString HeaderName = ToLowerCopy(TrimCopy(HeaderLine.substr(0, Colon)));
        const MString HeaderValue = TrimCopy(HeaderLine.substr(Colon + 1));
        OutRequest.Headers[HeaderName] = HeaderValue;
        LineStart = LineEnd + 2;
    }

    const size_t QueryIndex = OutRequest.RawTarget.find('?');
    if (QueryIndex == MString::npos)
    {
        OutRequest.Path = UrlDecode(OutRequest.RawTarget);
    }
    else
    {
        OutRequest.Path = UrlDecode(OutRequest.RawTarget.substr(0, QueryIndex));
        ParseQueryString(OutRequest.RawTarget.substr(QueryIndex + 1), OutRequest.QueryParams);
    }

    OutRequest.Body = RawRequest.substr(HeaderEnd + 4);
    return true;
}

FEditorHttpResponse MakeJsonErrorResponse(int StatusCode, const MString& ErrorCode, const MString& Message)
{
    MJsonWriter Writer = MJsonWriter::Object();
    Writer.Key("ok");
    Writer.Value(false);
    Writer.Key("error");
    Writer.Value(ErrorCode);
    Writer.Key("message");
    Writer.Value(Message);
    Writer.EndObject();

    FEditorHttpResponse Response;
    Response.StatusCode = StatusCode;
    Response.StatusText = StatusTextFromCode(StatusCode);
    Response.Body = Writer.ToString();
    return Response;
}

void SendAll(int ClientFd, const MString& Payload)
{
    const char* Data = Payload.data();
    size_t Remaining = Payload.size();
    while (Remaining > 0)
    {
        const int Sent = ::send(ClientFd, Data, static_cast<int>(Remaining), 0);
        if (Sent <= 0)
        {
            return;
        }
        Data += Sent;
        Remaining -= static_cast<size_t>(Sent);
    }
}
}

MEditorHttpServer::MEditorHttpServer(uint16 InPort, TRequestHandler InHandler)
    : Port(InPort)
    , RequestHandler(std::move(InHandler))
{
}

MEditorHttpServer::~MEditorHttpServer()
{
    Stop();
}

bool MEditorHttpServer::Start()
{
    if (bRunning.load())
    {
        return ListenFd.load() >= 0;
    }

    {
        std::lock_guard<std::mutex> Lock(StartMutex);
        bStartPending = true;
        bStartSucceeded = false;
    }

    bRunning.store(true);
    WorkerThread = std::thread(&MEditorHttpServer::RunLoop, this);

    std::unique_lock<std::mutex> Lock(StartMutex);
    StartCv.wait_for(
        Lock,
        std::chrono::milliseconds(1000),
        [this]()
        {
            return !bStartPending;
        });

    if (bStartPending)
    {
        LOG_ERROR("MObjectEditorService http start timed out on 127.0.0.1:%u", static_cast<unsigned>(Port));
        Lock.unlock();
        Stop();
        return false;
    }

    if (!bStartSucceeded)
    {
        Lock.unlock();
        Stop();
        return false;
    }

    LOG_INFO("MObjectEditorService listening on http://127.0.0.1:%u/", static_cast<unsigned>(Port));
    return true;
}

void MEditorHttpServer::Stop()
{
    if (!bRunning.exchange(false))
    {
        return;
    }

    const int Fd = ListenFd.exchange(-1);
    if (Fd >= 0)
    {
        MSocketPlatform::CloseSocket(Fd);
    }

    if (WorkerThread.joinable())
    {
        WorkerThread.join();
    }

    std::lock_guard<std::mutex> Lock(StartMutex);
    bStartPending = false;
    bStartSucceeded = false;
}

void MEditorHttpServer::SignalStartResult(bool bSucceeded)
{
    {
        std::lock_guard<std::mutex> Lock(StartMutex);
        bStartPending = false;
        bStartSucceeded = bSucceeded;
    }
    StartCv.notify_all();
}

void MEditorHttpServer::RunLoop()
{
    if (!MSocketPlatform::EnsureInit())
    {
        LOG_ERROR("MObjectEditorService http failed to initialize socket platform");
        SignalStartResult(false);
        return;
    }

    int ServerFd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (ServerFd < 0)
    {
        LOG_ERROR("MObjectEditorService http failed to create socket on port %u", static_cast<unsigned>(Port));
        SignalStartResult(false);
        return;
    }

    int Opt = 1;
    ::setsockopt(ServerFd, SOL_SOCKET, SO_REUSEADDR, (char*)&Opt, sizeof(Opt));

    sockaddr_in Addr;
    std::memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(Port);
    Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (::bind(ServerFd, (sockaddr*)&Addr, sizeof(Addr)) != 0)
    {
        LOG_ERROR("MObjectEditorService http bind failed on 127.0.0.1:%u", static_cast<unsigned>(Port));
        MSocketPlatform::CloseSocket(ServerFd);
        SignalStartResult(false);
        return;
    }

    if (::listen(ServerFd, 16) != 0)
    {
        LOG_ERROR("MObjectEditorService http listen failed on 127.0.0.1:%u", static_cast<unsigned>(Port));
        MSocketPlatform::CloseSocket(ServerFd);
        SignalStartResult(false);
        return;
    }

    ListenFd.store(ServerFd);
    SignalStartResult(true);

    while (bRunning.load())
    {
        sockaddr_in ClientAddr;
        socklen_t ClientLen = sizeof(ClientAddr);
        const int ClientFd = ::accept(ServerFd, (sockaddr*)&ClientAddr, &ClientLen);
        if (ClientFd < 0)
        {
            if (!bRunning.load())
            {
                break;
            }
            LOG_WARN("MObjectEditorService http accept failed on 127.0.0.1:%u", static_cast<unsigned>(Port));
            continue;
        }

        HandleClient(ClientFd);
        MSocketPlatform::CloseSocket(ClientFd);
    }

    const int Fd = ListenFd.exchange(-1);
    if (Fd >= 0)
    {
        MSocketPlatform::CloseSocket(Fd);
    }
}

void MEditorHttpServer::HandleClient(int ClientFd)
{
    char Buffer[4096];
    MString RawRequest;
    RawRequest.reserve(4096);

    size_t HeaderEnd = MString::npos;
    while (HeaderEnd == MString::npos && RawRequest.size() < MaxHeaderBytes)
    {
        const int Received = ::recv(ClientFd, Buffer, sizeof(Buffer), 0);
        if (Received <= 0)
        {
            return;
        }

        RawRequest.append(Buffer, Buffer + Received);
        HeaderEnd = RawRequest.find("\r\n\r\n");
    }

    if (HeaderEnd == MString::npos)
    {
        SendAll(ClientFd, "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
        return;
    }

    size_t ContentLength = 0;
    {
        const size_t FirstLineEnd = RawRequest.find("\r\n");
        size_t LineStart = (FirstLineEnd == MString::npos) ? 0 : FirstLineEnd + 2;
        while (LineStart < HeaderEnd)
        {
            const size_t LineEnd = RawRequest.find("\r\n", LineStart);
            if (LineEnd == MString::npos || LineEnd > HeaderEnd)
            {
                break;
            }

            const MString HeaderLine = RawRequest.substr(LineStart, LineEnd - LineStart);
            const size_t Colon = HeaderLine.find(':');
            if (Colon != MString::npos)
            {
                const MString Name = ToLowerCopy(TrimCopy(HeaderLine.substr(0, Colon)));
                const MString Value = TrimCopy(HeaderLine.substr(Colon + 1));
                if (Name == "content-length")
                {
                    try
                    {
                        ContentLength = static_cast<size_t>(std::stoull(Value));
                    }
                    catch (...)
                    {
                        ContentLength = 0;
                    }
                    break;
                }
            }

            LineStart = LineEnd + 2;
        }
    }

    if (ContentLength > MaxBodyBytes)
    {
        const FEditorHttpResponse ErrorResponse = MakeJsonErrorResponse(413, "payload_too_large", "Request payload is too large");
        MString ResponseText;
        ResponseText += "HTTP/1.1 413 Payload Too Large\r\n";
        ResponseText += "Content-Type: application/json; charset=utf-8\r\n";
        ResponseText += "Access-Control-Allow-Origin: *\r\n";
        ResponseText += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        ResponseText += "Access-Control-Allow-Headers: Content-Type\r\n";
        ResponseText += "Connection: close\r\n";
        ResponseText += "Content-Length: ";
        ResponseText += std::to_string(ErrorResponse.Body.size());
        ResponseText += "\r\n\r\n";
        ResponseText += ErrorResponse.Body;
        SendAll(ClientFd, ResponseText);
        return;
    }

    const size_t BodyOffset = HeaderEnd + 4;
    while (RawRequest.size() < BodyOffset + ContentLength)
    {
        const int Received = ::recv(ClientFd, Buffer, sizeof(Buffer), 0);
        if (Received <= 0)
        {
            break;
        }

        RawRequest.append(Buffer, Buffer + Received);
        if (RawRequest.size() > BodyOffset + MaxBodyBytes)
        {
            break;
        }
    }

    FEditorHttpRequest Request;
    MString ParseError;
    if (!TryParseRequest(RawRequest, Request, ParseError))
    {
        const FEditorHttpResponse ErrorResponse = MakeJsonErrorResponse(400, "bad_request", ParseError);
        MString ResponseText;
        ResponseText += "HTTP/1.1 400 Bad Request\r\n";
        ResponseText += "Content-Type: application/json; charset=utf-8\r\n";
        ResponseText += "Access-Control-Allow-Origin: *\r\n";
        ResponseText += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
        ResponseText += "Access-Control-Allow-Headers: Content-Type\r\n";
        ResponseText += "Connection: close\r\n";
        ResponseText += "Content-Length: ";
        ResponseText += std::to_string(ErrorResponse.Body.size());
        ResponseText += "\r\n\r\n";
        ResponseText += ErrorResponse.Body;
        SendAll(ClientFd, ResponseText);
        return;
    }

    FEditorHttpResponse Response;
    if (Request.Method == EEditorHttpMethod::Options)
    {
        Response.StatusCode = 204;
        Response.StatusText = "No Content";
        Response.ContentType.clear();
    }
    else if (RequestHandler)
    {
        Response = RequestHandler(Request);
    }
    else
    {
        Response = MakeJsonErrorResponse(500, "request_handler_missing", "Request handler is not configured");
    }

    if (Response.StatusText.empty())
    {
        Response.StatusText = StatusTextFromCode(Response.StatusCode);
    }

    MString ResponseText;
    ResponseText += "HTTP/1.1 ";
    ResponseText += std::to_string(Response.StatusCode);
    ResponseText += " ";
    ResponseText += Response.StatusText;
    ResponseText += "\r\n";

    if (!Response.ContentType.empty())
    {
        ResponseText += "Content-Type: ";
        ResponseText += Response.ContentType;
        ResponseText += "\r\n";
    }

    ResponseText += "Access-Control-Allow-Origin: *\r\n";
    ResponseText += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    ResponseText += "Access-Control-Allow-Headers: Content-Type\r\n";
    for (const auto& Pair : Response.Headers)
    {
        ResponseText += Pair.first;
        ResponseText += ": ";
        ResponseText += Pair.second;
        ResponseText += "\r\n";
    }
    ResponseText += "Connection: close\r\n";
    ResponseText += "Content-Length: ";
    ResponseText += std::to_string(Response.Body.size());
    ResponseText += "\r\n\r\n";
    ResponseText += Response.Body;
    SendAll(ClientFd, ResponseText);
}
