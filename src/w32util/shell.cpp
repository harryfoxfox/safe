/*
  Safe: Encrypted File System
  Copyright (C) 2014 Rian Hunter <rian@alum.mit.edu>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>
 */

#include <w32util/shell.hpp>

#include <safe/deferred.hpp>
#include <w32util/error.hpp>
#include <w32util/string.hpp>

#include <string>

#include <cassert>

#include <windows.h>
#include <shlobj.h>

namespace w32util {

std::string
get_folder_path(int folder, DWORD flags) {
  wchar_t app_directory_buf[MAX_PATH + 1];
  w32util::check_good_call(S_OK, SHGetFolderPathW,
                           nullptr, folder, nullptr, flags,
                           app_directory_buf);
  return w32util::narrow(app_directory_buf);
}

template <class T>
IPersistFile *
get_persist_file_iface(T *obj) {
  const GUID myIID_IPersistFile = {0x0000010b, 0x0000, 0x0000,
                                   {0xC0, 0x00,
                                    0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

  // create shell link persister
  IPersistFile *ppf;
  w32util::check_hresult([&] () {
      return obj->QueryInterface(myIID_IPersistFile, (LPVOID *) &ppf);
    });

  return ppf;
}

void
create_shortcut(std::string path, std::string target) {
  assert("CoInitialize has been called");

  // create shell link
  IShellLinkW *psl;
  w32util::check_hresult(CoCreateInstance,
                         CLSID_ShellLink, nullptr,
                         CLSCTX_INPROC_SERVER,
                         IID_IShellLink, (LPVOID *) &psl);
  auto _free_psl =
    safe::create_deferred([&] () { return psl->Release(); });

  // set target for shell link
  psl->SetPath(w32util::widen(target).c_str());

  // get IID_IPersistFile
  auto ppf = get_persist_file_iface(psl);
  auto _free_ppf =
    safe::create_deferred([&] () { return ppf->Release(); });

  // persist shell link on disk
  w32util::check_hresult([&] () {
      return ppf->Save(w32util::widen(path).c_str(), TRUE);
    });
}

std::string
get_shortcut_target(std::string path) {
  assert("CoInitialize has been called");

  // create shell link
  IShellLinkW *psl;
  w32util::check_hresult(CoCreateInstance,
                         CLSID_ShellLink, nullptr,
                         CLSCTX_INPROC_SERVER,
                         IID_IShellLink, (LPVOID *) &psl);
  auto _free_psl =
    safe::create_deferred([&] () { return psl->Release(); });

  // get IID_IPersistFile
  auto ppf = get_persist_file_iface(psl);
  auto _free_ppf =
    safe::create_deferred([&] () { return ppf->Release(); });

  // load shortcut
  w32util::check_hresult([&] () {
      return ppf->Load(w32util::widen(path).c_str(), STGM_READ);
    });

  // resolve the link (since the user may have moved the exe around)
  w32util::check_hresult([&] () {
      return psl->Resolve(nullptr, SLR_NO_UI | SLR_UPDATE);
    });

  // get the path to the link target
  WIN32_FIND_DATA fd;
  WCHAR target_w[MAX_PATH];
  w32util::check_hresult([&] () {
      return psl->GetPath(target_w, MAX_PATH, &fd, 0);
    });

  return w32util::narrow(target_w);
}

}
