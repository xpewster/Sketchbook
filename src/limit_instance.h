#include <windows.h>
#include "log.hpp"

class CLimitSingleInstance
{
    protected:
    DWORD  m_dwLastError;
    HANDLE m_hMutex;

    public:
    CLimitSingleInstance(const TCHAR *strMutexName)
    {
        //Unique name for this application
        m_hMutex = CreateMutex(NULL, FALSE, strMutexName); 
        m_dwLastError = GetLastError();
        if (m_hMutex == NULL && m_dwLastError != ERROR_ALREADY_EXISTS) 
        {
            LOG_ERROR << "Error creating mutex: " << m_dwLastError << "\n";
        }
    }
    
    ~CLimitSingleInstance() 
    {
        if (m_hMutex)  //Do not forget to close handles.
        {
        CloseHandle(m_hMutex); //Do as late as possible.
        m_hMutex = NULL; //Good habit to be in.
        }
    }

    BOOL IsAnotherInstanceRunning() 
    {
        return (ERROR_ALREADY_EXISTS == m_dwLastError);
    }
};