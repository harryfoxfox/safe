#include <encfs/cipher/MemoryPool.h>

#include <lockbox/lockbox_server.hpp>

#include <iostream>

enum main_return_code {
  MAIN_RETURN_CODE_SUCCESS,
  MAIN_RETURN_CODE_BAD_DATA,
  MAIN_RETURN_CODE_BAD_ARGS,
};

static
void
clear_string(std::string & str) {
  str.assign(str.size(), '\0');
}

int
main(int argc, char *argv[]) {
  if (argc < 2) return MAIN_RETURN_CODE_BAD_ARGS;

  std::cout << "Enter Your Password: " << std::endl;
  std::string password;
  std::getline(std::cin, password);

  auto secure_password = encfs::SecureMem(password.size() + 1);
  memmove(secure_password.data(), password.c_str(), password.size() + 1);
  clear_string(password);

  auto base_fs = lockbox::create_base_fs();

  auto encrypted_folder_path = base_fs->pathFromString(argv[1]);

  auto encfs_config = lockbox::read_encfs_config(base_fs, encrypted_folder_path);

  auto enc_fs = lockbox::create_enc_fs(std::move(base_fs), encrypted_folder_path,
                                       encfs_config, std::move(secure_password));

  // write a file
  auto path = encrypted_folder_path.join("sup");

  std::string test_string = "SUP";

  auto f = enc_fs->openfile(path, true, true);
  f.write(0, test_string);

  auto data = f.read(0, 3);
  if (data != test_string) {
    std::cerr << "DATA WAS NOT EQUAL: \"" << data << "\" VS \"" << test_string << "\"" << std::endl;
    return MAIN_RETURN_CODE_BAD_DATA;
  }

  return MAIN_RETURN_CODE_SUCCESS;
}
