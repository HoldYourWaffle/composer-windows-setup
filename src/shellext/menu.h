#pragma once
#include "shared.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>

// struct for storing menu items
struct CSMENUREC {
    int id;
    wchar_t title[60];
    wchar_t help[60];
    wchar_t cmd[60];
};

typedef CSMENUREC *LPCSMENUREC;

// enum for menu data ids 
enum CSDATAIDS {
    CMD_COMPOSER,
    CMD_OPTIONS,
    CMD_SHELL,
    CMD_INIT,
    CMD_INSTALL,
    CMD_INSTALL_DST,
    CMD_INSTALL_SRC,
    CMD_UPDATE,
    CMD_UPDATE_DST,
    CMD_UPDATE_SRC,
    CMD_DUMP_AUTOLOAD,
    CMD_DUMP_AUTOLOAD_OPT,
    CMD_SELF_UPDATE,
    CMD_HELP,
    CMD_RUNAS,
    CMD_SETTINGS
};

const CSMENUREC CS_MENUDATA[] = {
    {CMD_COMPOSER, L"Composer", L"Run Composer commands here", L""},
    {CMD_OPTIONS, L"Composer Options", L"See more commands", L""},
    {CMD_SHELL, L"Use Composer here", L"Open console in this directory", L""},
    {CMD_INIT, L"Composer Init", L"Configure a new package in this directory", L"init"},
    {CMD_INSTALL, L"Composer Install", L"Install dependencies in this directory", L"install"},
    {CMD_INSTALL_DST, L"Install prefer-dist", L"Install dependencies - distribution", L"install --prefer-dist"},
    {CMD_INSTALL_SRC, L"Install prefer-source", L"Install dependencies - source", L"install --prefer-source"},
    {CMD_UPDATE, L"Composer Update", L"Update dependencies in this directory", L"update"},
    {CMD_UPDATE_DST, L"Update prefer-dist", L"Update dependencies - distribution", L"update --prefer-dist"},
    {CMD_UPDATE_SRC, L"Update prefer-source", L"Install dependencies - source", L"update --prefer-source"},
    {CMD_DUMP_AUTOLOAD, L"Dump-Autoload", L"Update the autoloader for class-map dependencies", L"dump-autoload"},
    {CMD_DUMP_AUTOLOAD_OPT, L"Dump-Autoload optimize", L"Convert PSR-0 autoloading to a class-map", L"dump-autoload --optimize"},
    {CMD_SELF_UPDATE, L"Self-Update", L"Install the latest version of Composer", L"self-update"},
    {CMD_HELP, L"Show Help", L"View usage information", L""},
    {CMD_RUNAS, L"Run as admin (Install/Update)", L"Run Install/Update commands as administrator", L""},
    {CMD_SETTINGS, L"Settings...", L"Change console program and menu display", L""}
};

// struct for holding displayed menu items 
struct CSDISPLAYREC {
    int cmdId;
    int dataId;
};

// struct for holding target directory info
struct CSTARGET {
    BOOLEAN invalid;
    BOOLEAN composer;
    BOOLEAN installed;
    std::wstring dir;
} ;

// struct for holding console shell reg info
struct CSREG {
    std::wstring cmd;
    std::wstring open;
    std::wstring run;
};

typedef CSREG *LPCSREG;

// struct for holding console command info
struct CSCMD {
    std::wstring cmd;
    std::wstring verb;
    std::wstring params;
    std::wstring dir;
};

typedef CSCMD *LPCSCMD;

class ComposerShellMenu : public IShellExtInit, public IContextMenu
{
public:
    ComposerShellMenu(HMODULE hModule);

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void **ppv);
    IFACEMETHODIMP_(ULONG) AddRef();
    IFACEMETHODIMP_(ULONG) Release();

    // IShellExtInit
    IFACEMETHODIMP Initialize(LPCITEMIDLIST pidlFolder, LPDATAOBJECT pDataObj, HKEY hKeyProgID);

    // IContextMenu
    IFACEMETHODIMP QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags);
    IFACEMETHODIMP InvokeCommand(LPCMINVOKECOMMANDINFO pici);
    IFACEMETHODIMP GetCommandString(UINT_PTR idCommand, UINT uFlags, UINT *pwReserved, LPSTR pszName, UINT cchMax);

protected:
    ~ComposerShellMenu(void);

private:
    void ClearStorage();
    BOOLEAN ConsoleFromReg(LPCSREG reg);
    CSCMD ConsoleGetCmd(CSMENUREC menuRec);
    void ConsoleSetCmd(const std::wstring& param, const std::wstring& args, LPCSCMD cmd);
    BOOLEAN DataRecFromId(UINT dataId, LPCSMENUREC pMenuRec);
    BOOLEAN DataRecFromCmd(UINT_PTR cmdId, LPCSMENUREC pMenuRec);
    BOOLEAN MenuAdd(UINT dataId, HMENU hMenu, HMENU hSub, UINT idCmdFirst, PUINT cmdId, PUINT position, PDWORD error);
    BOOLEAN MenuBuild(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, PDWORD result);
    BOOLEAN MenuExists(HMENU hMenu);
    BOOLEAN MenuSeparator(HMENU hMenu, PUINT cmdId, PUINT position, PDWORD error);
    std::wstring RegReadString(HKEY hKey, const std::wstring& name);
    BOOLEAN RequiresElevation(int cmdId);
    HRESULT ShellGetItem(IShellItem ** newItem, PBOOLEAN isFile);
    HRESULT ShellGetItemParent(IShellItem* child, IShellItem ** parent);
    HRESULT ShellIsItemValid(IShellItem* item, BOOLEAN requireFolder, PBOOLEAN isFile);
    HRESULT ShellItemGetData(IShellItem* item);
    HRESULT TargetGetData();
    void TargetSetValues(LPWSTR target);
    
    HMODULE m_Module;
    long m_RefCount;
    LPDATAOBJECT m_ShellData;
    LPITEMIDLIST m_ShellFolder;
    CSTARGET m_Target;
    CSMENUVARS m_Vars;
    std::vector<CSDISPLAYREC> m_DisplayList;
};