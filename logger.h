#ifndef DONT_USE_STB
    #ifndef DONT_IMPLEMENT_STB
        #define STB_SPRINTF_IMPLEMENTATION
    #endif
    #include "stb_sprintf.h"

    #define vsnprintf stbsp_vsnprintf
#endif
#include <string.h>
#include <android/log.h>
enum eLogPrio
{
    LogP_Unk = 0,
    LogP_Default,
    LogP_Verbose,
    LogP_Debug,
    LogP_Info,
    LogP_Warn,
    LogP_Error,
    LogP_Fatal,
    LogP_Silent,
};

class Logger
{
public:
    Logger();
    void ToggleOutput(bool enabled);
    void SetTag(const char* szTag);
    void Print(eLogPrio prio, const char* szMessage, ...);
    void PrintV(eLogPrio prio, const char* szMessage, va_list args);
    void PrintTag(eLogPrio prio, const char* szTag, const char* szMessage, ...);
    void PrintTagV(eLogPrio prio, const char* szTag, const char* szMessage, va_list args);
    void Info(const char* szMessage, ...);
    void InfoV(const char* szMessage, va_list args);
    void Error(const char* szMessage, ...);
    void ErrorV(const char* szMessage, va_list args);
    void If(bool cond, const char* szMessage, ...);
    void IfV(bool cond, const char* szMessage, va_list args);
private:
    const char* m_szTag;
    bool m_bEnabled;
};

Logger::Logger()
{
    strncpy(m_szTag, "AML Mod", sizeof(m_szTag));
    m_bEnabled = true;
    m_fnLogCallback = NULL;
    m_fnNewTagCallback = NULL;
    m_fnToggledCallback = NULL;
}

void Logger::ToggleOutput(bool enabled)
{
    if(m_bEnabled != enabled)
    {
        m_bEnabled = enabled;
        if(m_fnToggledCallback) m_fnToggledCallback(enabled);
    }
}

void Logger::SetTag(const char* szTag)
{
    if(m_fnNewTagCallback) m_fnNewTagCallback(m_szTag, szTag);
    strncpy(m_szTag, szTag, sizeof(m_szTag)-1);
    m_szTag[sizeof(m_szTag)-1] = 0;
}

void Logger::Print(eLogPrio prio, const char* szMessage, ...)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    va_list args;
    va_start(args, szMessage);
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(prio, buffer);
    __android_log_write((android_LogPriority)prio, m_szTag, buffer);
    va_end(args);
#endif
}

void Logger::PrintV(eLogPrio prio, const char* szMessage, va_list args)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(prio, buffer);
    __android_log_write((android_LogPriority)prio, m_szTag, buffer);
#endif
}

void Logger::PrintTag(eLogPrio prio, const char* szTag, const char* szMessage, ...)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    va_list args;
    va_start(args, szMessage);
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(prio, buffer);
    __android_log_write((android_LogPriority)prio, m_szTag, buffer);
    va_end(args);
#endif
}

void Logger::PrintTagV(eLogPrio prio, const char* szTag, const char* szMessage, va_list args)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(prio, buffer);
    __android_log_write((android_LogPriority)prio, m_szTag, buffer);
#endif
}

void Logger::Info(const char* szMessage, ...)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    va_list args;
    va_start(args, szMessage);
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(LogP_Info, buffer);
    __android_log_write(ANDROID_LOG_INFO, m_szTag, buffer);
    va_end(args);
#endif
}

void Logger::InfoV(const char* szMessage, va_list args)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(LogP_Info, buffer);
    __android_log_write(ANDROID_LOG_INFO, m_szTag, buffer);
#endif
}

void Logger::Error(const char* szMessage, ...)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    va_list args;
    va_start(args, szMessage);
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(LogP_Error, buffer);
    __android_log_write(ANDROID_LOG_ERROR, m_szTag, buffer);
    va_end(args);
#endif
}

void Logger::ErrorV(const char* szMessage, va_list args)
{
#ifndef NOLOGGING
    if(!m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(LogP_Error, buffer);
    __android_log_write(ANDROID_LOG_ERROR, m_szTag, buffer);
#endif
}

void Logger::If(bool cond, const char* szMessage, ...)
{
#ifndef NOLOGGING
    if(cond || !m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    va_list args;
    va_start(args, szMessage);
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(LogP_Fatal, buffer);
    __android_log_write(ANDROID_LOG_FATAL, m_szTag, buffer);
    va_end(args);
#endif
}

void Logger::IfV(bool cond, const char* szMessage, va_list args)
{
#ifndef NOLOGGING
    if(cond || !m_bEnabled) return;
    
    char buffer[TMPBUF_SIZE];
    vsnprintf(buffer, sizeof(buffer), szMessage, args);
    if(m_fnLogCallback) m_fnLogCallback(LogP_Fatal, buffer);
    __android_log_write(ANDROID_LOG_FATAL, m_szTag, buffer);
#endif
}
static Logger loggerLocal;
Logger* logger = &loggerLocal;
