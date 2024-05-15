//
// Created by RED on 15.05.2024.
//

#ifndef STUDIOMDL_V2_STUDIOMDL_ERRORS_H
#define STUDIOMDL_V2_STUDIOMDL_ERRORS_H

#include "common/cmdlib.h"

void TokenError(const char *fmt, ...);
void MdlError(const char *fmt, ...);
void MdlWarning(const char *fmt, ...);
void MdlExceptionFilter(unsigned long code);
long __stdcall VExceptionFilter(struct _EXCEPTION_POINTERS *ExceptionInfo);

class CMdlLoggingListener : public CCmdLibStandardLoggingListener {
    virtual void Log(const LoggingContext_t *pContext, const tchar *pMessage);
};

#endif //STUDIOMDL_V2_STUDIOMDL_ERRORS_H
