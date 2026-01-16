#pragma once
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <iostream>
#include "log.hpp"

#pragma comment(lib, "winhttp.lib")

struct HttpResponse {
    int statusCode = 0;
    std::string body;
    
    bool isOk() const { return statusCode == 200; }
};

HttpResponse get(const std::string& url) {
    HttpResponse response;
    
    // Convert to wide string
    std::wstring wUrl(url.begin(), url.end());
    
    // Parse URL
    URL_COMPONENTS urlComp = { sizeof(urlComp) };
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    urlComp.lpszHostName = host;
    urlComp.dwHostNameLength = sizeof(host) / sizeof(wchar_t);
    urlComp.lpszUrlPath = path;
    urlComp.dwUrlPathLength = sizeof(path) / sizeof(wchar_t);
    
    if (!WinHttpCrackUrl(wUrl.c_str(), 0, 0, &urlComp)) {
        LOG_WARN << "Failed to parse URL: " << url << "\n";
        return response;
    }
    
    // Open session
    HINTERNET hSession = WinHttpOpen(
        L"SystemMonitor/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!hSession) {
        LOG_WARN << "WinHttpOpen failed\n";
        return response;
    }
    
    // Connect to server
    INTERNET_PORT port = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) 
                        ? INTERNET_DEFAULT_HTTPS_PORT 
                        : INTERNET_DEFAULT_HTTP_PORT;
    
    HINTERNET hConnect = WinHttpConnect(hSession, host, port, 0);
    if (!hConnect) {
        LOG_WARN << "WinHttpConnect failed\n";
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Open request
    DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) 
                ? WINHTTP_FLAG_SECURE 
                : 0;
    
    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags
    );
    
    if (!hRequest) {
        LOG_WARN << "WinHttpOpenRequest failed\n";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Send request
    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        LOG_WARN << "WinHttpSendRequest failed\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Receive response
    if (!WinHttpReceiveResponse(hRequest, NULL)) {
        LOG_WARN << "WinHttpReceiveResponse failed\n";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return response;
    }
    
    // Get status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX,
                        &statusCode,
                        &statusCodeSize,
                        WINHTTP_NO_HEADER_INDEX);
    response.statusCode = statusCode;
    
    // Read data
    DWORD bytesAvailable = 0;
    while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0) {
        std::vector<char> buffer(bytesAvailable);
        DWORD bytesRead = 0;
        
        if (WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead)) {
            response.body.append(buffer.data(), bytesRead);
        }
    }
    
    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return response;
}
