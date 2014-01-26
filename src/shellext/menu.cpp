#include "menu.h"
#include "types.h"
#include "utils.h"
#include "shared.h"
#include <stdio.h>
#include <new>
#include <Objbase.h>
#include <strsafe.h>
#include <Shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

extern long g_DllRef;
extern DllShared g_Shared;

ComposerShellMenu::ComposerShellMenu(HMODULE hModule) :
    m_Module(hModule),
    m_RefCount(1),
    m_ShellData(NULL),
    m_ShellFolder(NULL)
{
    InterlockedIncrement(&g_DllRef);
}

ComposerShellMenu::~ComposerShellMenu(void)
{
    ClearStorage();
    InterlockedDecrement(&g_DllRef);
}

// Query to the interface the component supported.
IFACEMETHODIMP ComposerShellMenu::QueryInterface(REFIID riid, void **ppv)
{
    static const QITAB qit[] = 
    {
        QITABENT(ComposerShellMenu, IContextMenu),
        QITABENT(ComposerShellMenu, IShellExtInit), 
        { 0 },
    };
    return QISearch(this, qit, riid, ppv);
}

// Increase the reference count for an interface on an object.
IFACEMETHODIMP_(ULONG) ComposerShellMenu::AddRef()
{
    return InterlockedIncrement(&m_RefCount);
}

// Decrease the reference count for an interface on an object.
IFACEMETHODIMP_(ULONG) ComposerShellMenu::Release()
{
    ULONG cRef = InterlockedDecrement(&m_RefCount);
    if (0 == cRef)
        delete this;

    return cRef;
}

// Initialize the context menu handler.
IFACEMETHODIMP ComposerShellMenu::Initialize(
    LPCITEMIDLIST pidlFolder, LPDATAOBJECT pDataObj, HKEY hKeyProgID)
{
    // We store the input for any subsequent QueryContextMenu call
    ClearStorage();
    HRESULT hr = E_INVALIDARG;

    if (pidlFolder)
    {
        m_ShellFolder = ILClone(pidlFolder);
        hr = m_ShellFolder ? S_OK : E_OUTOFMEMORY;
    }
    else if (pDataObj)
    {
        m_ShellData = pDataObj; 
        m_ShellData->AddRef();
        hr = S_OK;
    }

    return hr;
}

IFACEMETHODIMP ComposerShellMenu::QueryContextMenu(
    HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{ 
    // Do nothing if uFlags include CMF_DEFAULTONLY.
    if (CMF_DEFAULTONLY & uFlags)
        return S_OK;
    
    // See if we have already written the menu.
    if (MenuExists(hMenu))
        return S_OK;

    // Get the target directory and status
    HRESULT hr = TargetGetData();

    if (SUCCEEDED(hr))
    {      
        // See if target is invalid
        if (m_Target.invalid)
            return S_OK;

        // Get our vars from shared storage
        m_Vars = g_Shared.GetMenuVars();
        DWORD result = 0;

        if (!MenuBuild(hMenu, indexMenu, idCmdFirst, &result))
        {
            hr = HRESULT_FROM_WIN32(result);
        }
        else
        {
            hr = MAKE_HRESULT(SEVERITY_SUCCESS, 0, USHORT(result));
        }
    }
        
    return hr;
}

IFACEMETHODIMP ComposerShellMenu::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{
    HRESULT hr = E_FAIL;
    CSMENUREC menuRec;
    
    if (DataRecFromCmd(LOWORD(pici->lpVerb), &menuRec))
    {      
        if (CMD_RUNAS == menuRec.id)
        {
            return g_Shared.ToggleRunas(m_Vars.elevate);   
        }
        else  
        {
            hr = S_OK;
            CSCMD cmd;

            if (CMD_SETTINGS == menuRec.id)
            {
                cmd.verb = L"open";
                cmd.cmd = m_Vars.settings;
                cmd.dir = m_Target.dir;
            }
            else
            {
                cmd = ConsoleGetCmd(menuRec);
            }

            ShellExecute(pici->hwnd, cmd.verb.c_str(), cmd.cmd.c_str(), cmd.params.c_str(),
                cmd.dir.c_str(), SW_SHOWNORMAL);            
        }
        
        hr = S_OK;
    }
    
    return hr;
}

IFACEMETHODIMP ComposerShellMenu::GetCommandString(UINT_PTR idCommand, 
    UINT uFlags, UINT *pwReserved, LPSTR pszName, UINT cchMax)
{
    CSMENUREC menuRec;
    
    if (!DataRecFromCmd(idCommand, &menuRec))
        return uFlags == GCS_VALIDATEW ? S_FALSE : E_INVALIDARG; 

    HRESULT hr = E_INVALIDARG;

    if (GCS_HELPTEXTW == uFlags)
        hr = StringCchCopy(reinterpret_cast<PWSTR>(pszName), cchMax, menuRec.help);
    
    return hr;
}

void ComposerShellMenu::ClearStorage()
{
    CoTaskMemFree(m_ShellFolder);
    m_ShellFolder = NULL;
    
    if (m_ShellData)
    { 
        m_ShellData->Release();
        m_ShellData = NULL;
    }
}

BOOLEAN ComposerShellMenu::ConsoleFromReg(LPCSREG reg)
{
    BOOLEAN result = false;
    HKEY hKey = NULL;
        
    if (utils::RegUserOpen(&hKey))
    {
        // Read reg values
        reg->cmd = utils::Trim(RegReadString(hKey, COMPOSER_REG_SHELLCMD));
        reg->open = utils::Trim(RegReadString(hKey, COMPOSER_REG_SHELLOPEN));
        reg->run = utils::Trim(RegReadString(hKey, COMPOSER_REG_SHELLRUN));

        RegCloseKey(hKey);

        // Check values
        result = (utils::FileExists(reg->cmd) && !reg->open.empty() && !reg->run.empty());
    }

    return result;
}

CSCMD ComposerShellMenu::ConsoleGetCmd(CSMENUREC menuRec)
{
    CSCMD cmdRec;
    CSREG reg;
    std::wstring param, args;
        
    if (!ConsoleFromReg(&reg))
    {
        reg.cmd = g_Shared.GetExeCmd();
        reg.open = L"/k [d]";
        reg.run = L"/k {s} \"{d}\" {a}";
    }

    cmdRec.cmd = reg.cmd;
    
    if (RequiresElevation(menuRec.id))
    {
        cmdRec.verb = L"runas";
        param = reg.run;
        args = menuRec.cmd;
    }
    else
    {
        cmdRec.verb = L"open";
        
        if (CMD_SHELL == menuRec.id)
        {
            param = reg.open;        
            args = COMPOSER_SHELL_OPEN;
        }
        else
        {
            param = reg.run;
            args = menuRec.cmd;
        }
    }

    ConsoleSetCmd(param, args, &cmdRec);
    
    return cmdRec;
}

void ComposerShellMenu::ConsoleSetCmd(const std::wstring& param, const std::wstring& args, LPCSCMD cmd)
{
    std::wstring s = param;
    std::wstring dmod = L"[d]";     
    
    if (std::wstring::npos != s.find(dmod))
    {
        s = utils::StringReplace(s, dmod, L""); 
        cmd->dir = m_Target.dir;
    }

    /* We need to translate all backslashes to forward slashes.
    This is important because Unixy shells read backslashes as
    an escape character and will not receive the correct params */

    s = utils::StringReplace(s, L"{s}", COMPOSER_SHELL_SCRIPT);
    s = utils::StringReplace(s, L"{d}", utils::StringReplace(m_Target.dir, L"\\", L"/"));
    s = utils::StringReplace(s, L"{a}", utils::StringReplace(args, L"\\", L"/"));

    cmd->params = utils::Trim(s);
}

BOOLEAN ComposerShellMenu::DataRecFromId(UINT dataId, LPCSMENUREC pMenuRec)
{
    for (int i = 0; i < ARRAYSIZE(CS_MENUDATA); ++i)
    {
        if (dataId == CS_MENUDATA[i].id)
        {
            *pMenuRec = CS_MENUDATA[i]; 
            return true;
        }
    }

    return false;
}

BOOLEAN ComposerShellMenu::DataRecFromCmd(UINT_PTR cmdId, LPCSMENUREC pMenuRec)
{
    int count = int(m_DisplayList.size());

    for (int i = 0; i < count; ++i)
    {
        if (cmdId == m_DisplayList[i].cmdId)
            return DataRecFromId(m_DisplayList[i].dataId, pMenuRec);
    }

    return false;
}

BOOLEAN ComposerShellMenu::MenuAdd(UINT dataId, HMENU hMenu, HMENU hSub, UINT idCmdFirst, PUINT cmdId, PUINT position, PDWORD error)
{
    CSMENUREC menuRec;
    
    // safeguard
    if (!DataRecFromId(dataId, &menuRec))
    {
        *error = 87;
        return false;
    }

    MENUITEMINFO mii;
    memset(&mii, 0, sizeof(MENUITEMINFO));
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_STRING | MIIM_DATA | MIIM_ID;
    mii.wID = idCmdFirst + *cmdId;
    mii.dwItemData = (ULONG_PTR)m_Module;
    mii.dwTypeData = menuRec.title;
    
    if (hSub)
    {
        mii.fMask |= MIIM_SUBMENU;
        mii.hSubMenu = hSub;
    }

    if (CMD_RUNAS == dataId && m_Vars.elevate)
    {
        mii.fMask |= MIIM_STATE;
        mii.fState = MFS_CHECKED; 
    }
    else if (CMD_COMPOSER == dataId)
    {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = m_Vars.hBmpComposer;
    }
    else if (RequiresElevation(dataId))
    {
        mii.fMask |= MIIM_BITMAP;
        mii.hbmpItem = m_Vars.hBmpUac;
    }
    
    if (!InsertMenuItem(hMenu, *position, TRUE, &mii))
    {
        *error = GetLastError();
        return false;
    }

    // add record to display list
    CSDISPLAYREC rec = {*cmdId, dataId};
    m_DisplayList.push_back(rec);

    *cmdId += 1;
    *position += 1;

    return true;
}


// On success returns TRUE with incremented cmdId in result
// On failure returns FALSE with Win32 error code in result
BOOLEAN ComposerShellMenu::MenuBuild(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, PDWORD result)
{
    UINT cmdId = 0;
    HMENU hMain = m_Vars.collapse ? CreatePopupMenu() : hMenu;
    UINT posMain = m_Vars.collapse ? 0 : indexMenu;
    HMENU hOptions = CreatePopupMenu();
    UINT posOpts = 0;

    if (!m_Vars.collapse)
    {
        // main menu separator
        if (!MenuSeparator(hMain, &cmdId, &posMain, result))
            return false;        
    }

    // init if not composer folder
    if (false == m_Target.composer)
    {
        if (!MenuAdd(CMD_INIT, hMain, 0, idCmdFirst, &cmdId, &posMain, result))
            return false;
        
    }
    else
    {
        // main menu install
        if (!MenuAdd(CMD_INSTALL, hMain, 0, idCmdFirst, &cmdId, &posMain, result))
            return false;

        if (m_Target.installed)
        {
            // main menu update if installed
            if (!MenuAdd(CMD_UPDATE, hMain, 0, idCmdFirst, &cmdId, &posMain, result))
                return false;
        }        
    }

    if (m_Target.composer)
    {
        // options menu install prefer-dist
        if (!MenuAdd(CMD_INSTALL_DST, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
            return false;

        // options menu install install prefer-source
        if (!MenuAdd(CMD_INSTALL_SRC, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
            return false;

        // options menu separator
        if (!MenuSeparator(hOptions, &cmdId, &posOpts, result))
            return false;
        
        if (m_Target.installed)
        {
            // options menu update prefer-dist
            if (!MenuAdd(CMD_UPDATE_DST, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
                return false;

            // options menu update prefer-source
            if (!MenuAdd(CMD_UPDATE_SRC, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
                return false;

            // options menu separator
            if (!MenuSeparator(hOptions, &cmdId, &posOpts, result))
                return false;
        }

        // options menu dump-autload
        if (!MenuAdd(CMD_DUMP_AUTOLOAD, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
            return false;

        // options menu dump-autoload --optimize
        if (!MenuAdd(CMD_DUMP_AUTOLOAD_OPT, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
            return false;

        // options menu separator
        if (!MenuSeparator(hOptions, &cmdId, &posOpts, result))
            return false;
    }

    // options menu self-update
    if (!MenuAdd(CMD_SELF_UPDATE, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
        return false;

    // options menu show help
    if (!MenuAdd(CMD_HELP, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
        return false;

    // options menu separator, if we have subsequent entries
    if (!m_Vars.isAdmin || !m_Vars.settings.empty())
    {        
        if (!MenuSeparator(hOptions, &cmdId, &posOpts, result))
            return false;
    }
    
    if (!m_Vars.isAdmin)
    {
        // options menu runas admin
        if (!MenuAdd(CMD_RUNAS, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
            return false;
    }

    if (!m_Vars.settings.empty())
    {
        // options menu settings
        if (!MenuAdd(CMD_SETTINGS, hOptions, 0, idCmdFirst, &cmdId, &posOpts, result))
            return false;
    }

    // Add the options submenu
    if (!MenuAdd(CMD_OPTIONS, hMain, hOptions, idCmdFirst, &cmdId, &posMain, result))
        return false;

    // main menu shell
    if (!MenuAdd(CMD_SHELL, hMain, 0, idCmdFirst, &cmdId, &posMain, result))
        return false;

    if (!m_Vars.collapse)
    {
        // main menu separator
        if (!MenuSeparator(hMain, &cmdId, &posMain, result))
            return false;
    }
    else
    {
        // Add the main submenu
        if (!MenuAdd(CMD_COMPOSER, hMenu, hMain, idCmdFirst, &cmdId, &indexMenu, result))
            return false;
    }
    
    *result = cmdId;
    return true;
}


BOOLEAN ComposerShellMenu::MenuExists(HMENU hMenu)
{
    MENUITEMINFO mii = { sizeof(mii) };
    mii.cbSize = sizeof(MENUITEMINFO);
    mii.fMask = MIIM_DATA;
    mii.dwTypeData = NULL;

    int count = GetMenuItemCount(hMenu);

    for (int i = 0; i < count; ++i)
    {
        GetMenuItemInfo(hMenu, i, TRUE, &mii);
        
        if (mii.dwItemData == (ULONG_PTR)m_Module)
            return true;
    }

    return false;
}

BOOLEAN ComposerShellMenu::MenuSeparator(HMENU hMenu, PUINT cmdId, PUINT position, PDWORD error)
{
    if (!InsertMenu(hMenu, *position, MF_SEPARATOR|MF_BYPOSITION, 0, NULL))
    {
        *error = GetLastError();
        return false;
    }
    else
    {
        *cmdId += 1;
        *position += 1;
        return true;
    }    
}

std::wstring ComposerShellMenu::RegReadString(HKEY hKey, const std::wstring& name)
{
    std::wstring result;
    DWORD flags = RRF_RT_REG_SZ;
    DWORD bytes = 0;
    wchar_t *pData = NULL;
    
    LONG regResult = RegGetValue(hKey, NULL, name.c_str(), flags, NULL,
        NULL, &bytes);

    if (ERROR_SUCCESS == regResult)
    {
        pData = (wchar_t *) malloc(bytes);
        
        if (pData)
        {
            regResult = RegGetValue(hKey, NULL, name.c_str(), flags,
                NULL, (LPBYTE)pData, &bytes);
            
            if (ERROR_SUCCESS == regResult)    
                result = pData;

            free(pData);
        }
    }

    return result;
}

BOOLEAN ComposerShellMenu::RequiresElevation(int cmdId)
{
    return (m_Vars.elevate && (
        CMD_INSTALL == cmdId ||
        CMD_INSTALL_DST == cmdId ||
        CMD_INSTALL_SRC == cmdId ||
        CMD_UPDATE == cmdId ||
        CMD_UPDATE_DST == cmdId ||
        CMD_UPDATE_SRC == cmdId
        ));
}

// Populates item. It is the responsibility of the caller to call item->Release()
// if the return value indicates success
HRESULT ComposerShellMenu::ShellGetItem(IShellItem ** item, PBOOLEAN isFile)
{
    HRESULT hr = E_FAIL;
    *item = NULL;
        
    if (m_ShellFolder)
    {
        hr = SHCreateItemFromIDList(m_ShellFolder, IID_PPV_ARGS(item));
    }
    else if (m_ShellData)
    {
        IShellItemArray *itemArray = NULL;
        hr = SHCreateShellItemArrayFromDataObject(m_ShellData, IID_PPV_ARGS(&itemArray));
        
        if (SUCCEEDED(hr))
        {
            DWORD count = 0;
            hr = itemArray->GetCount(&count);

            // We are only interested in single items
            if (SUCCEEDED(hr))
                hr = (1 == count) ? itemArray->GetItemAt(0, item) : E_FAIL;           
            
            itemArray->Release();
            itemArray = NULL;
        }
    }

    if (SUCCEEDED(hr))
    {
        // Check if we are valid
        hr = ShellIsItemValid(*item, false, isFile);

        if (FAILED(hr))
        {
            (*item)->Release();
            *item = NULL;
        }
    }

    return hr;
}

// Populates item. It is the responsibility of the caller to call item->Release()
// if the return value indicates success
HRESULT ComposerShellMenu::ShellGetItemParent(IShellItem* child, IShellItem ** item)
{
    BOOLEAN isFile = false;
    HRESULT hr = child->GetParent(item);

    if (SUCCEEDED(hr))
    {
        // Check if we are valid
        hr = ShellIsItemValid(*item, true, &isFile);

        if (FAILED(hr))
        {
            (*item)->Release();
            *item = NULL;
        }
    }

    return hr;
}

HRESULT ComposerShellMenu::ShellIsItemValid(IShellItem* item, BOOLEAN requireFolder, PBOOLEAN isFile)
{
    DWORD mask = SFGAO_FILESYSTEM | SFGAO_STREAM | SFGAO_FOLDER;
    DWORD attribs = 0;
    HRESULT hr = item->GetAttributes(mask, &attribs);

    // See if we have a com error
    if (S_OK != hr && S_FALSE != hr)
        return hr;

    // Ensure we have SFGAO_FILESYSTEM 
    if (!(attribs & SFGAO_FILESYSTEM))
        return E_FAIL;

    // A compressed file can have both SFGAO_STREAM and SFGAO_FOLDER
    BOOLEAN folder = (attribs & SFGAO_FOLDER) && !(attribs & SFGAO_STREAM);
    *isFile = !folder;
    
    return requireFolder && *isFile ? E_FAIL : S_OK;
}

HRESULT ComposerShellMenu::ShellItemGetData(IShellItem* item)
{
    LPWSTR filepath;
    HRESULT hr = item->GetDisplayName(SIGDN_FILESYSPATH, &filepath);
            
    if (SUCCEEDED(hr))
    {
        TargetSetValues(filepath);
        CoTaskMemFree(filepath);
    }

    return hr;
}

HRESULT ComposerShellMenu::TargetGetData()
{
    IShellItem* item = NULL;
    BOOLEAN isFile = false;
    HRESULT hr = ShellGetItem(&item, &isFile);
    
    if (SUCCEEDED(hr))
    {
        if (isFile)
        {
            IShellItem* parent = NULL;
            hr = ShellGetItemParent(item, &parent);

            if (SUCCEEDED(hr))
            {
                hr = ShellItemGetData(parent);
                parent->Release();
            }
        }
        else
        {
            hr = ShellItemGetData(item);
        }
        
        item->Release();
    }

    return hr;
}

void ComposerShellMenu::TargetSetValues(LPWSTR target)
{
    m_Target.invalid = false;
    m_Target.composer = false;
    m_Target.installed = false;
    m_Target.dir = target;
    
    // See if we are in vendor directory
    std::wstring path(m_Target.dir + L"\\");
    size_t pos = path.rfind(L"\\vendor\\");

    if (std::wstring::npos != pos)
    {
        path.resize(pos);
        m_Target.invalid = utils::DirectoryExists(path + L"\\vendor\\composer");
    }
    
    if (m_Target.invalid)
        return;
        
    // See it we have a composer.json here
    m_Target.composer = utils::FileExists(m_Target.dir + L"\\composer.json");

    if (m_Target.composer)
        // We have a composer.json. Check if we have installed it
        m_Target.installed = utils::FileExists(m_Target.dir + L"\\vendor\\composer\\installed.json");
}