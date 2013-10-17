#include <encfs/cipher/MemoryPool.h>

#include <lockbox/lockbox_server.hpp>

#include <iostream>

#include <cstring>

enum main_return_code {
  MAIN_RETURN_CODE_SUCCESS,
  MAIN_RETURN_CODE_BAD_DATA,
  MAIN_RETURN_CODE_BAD_ARGS,
  MAIN_RETURN_CODE_BAD_DIRECTORY,
  MAIN_RETURN_CODE_BAD_CONFIG_FILE,
  MAIN_RETURN_CODE_ENCRYPTED_PATH_IS_NOT_A_DIRECTORY,
};

static
void
clear_string(std::string & str) {
  str.assign(str.size(), '\0');
}

static
std::string
getline_ret(decltype(std::cin) & f) {
  std::string toret;
  getline(f, toret);
  return toret;
}

static encfs::SecureMem
read_password_from_console(const std::string & prompt) {
  std::cout << prompt << ": ";
  auto password = getline_ret(std::cin);
  std::cout << std::endl;

  auto secure_password = encfs::SecureMem(password.size() + 1);
  memmove(secure_password.data(), password.c_str(), password.size() + 1);
  clear_string(password);

  return std::move(secure_password);
}

int
main(int argc, char *argv[]) {
  if (argc < 2) return MAIN_RETURN_CODE_BAD_ARGS;

  auto native_fs = lockbox::create_native_fs();

  // TODO: elegantly deal with bad input paths
  opt::optional<encfs::Path> maybe_encrypted_directory_path;
  try {
    maybe_encrypted_directory_path = native_fs->pathFromString(argv[1]);
  }
  catch (const std::exception & err) {
    std::cerr << "Bad path for encrypted directory: " << argv[1] << std::endl;
    return MAIN_RETURN_CODE_BAD_DIRECTORY;
  }

  assert(maybe_encrypted_directory_path);

  auto encrypted_directory_path = std::move(*maybe_encrypted_directory_path);

  // attempt to read configuration
  opt::optional<encfs::EncfsConfig> maybe_encfs_config;
  try {
    maybe_encfs_config =
      encfs::read_config(native_fs, encrypted_directory_path);
  }
  catch (const encfs::ConfigurationFileDoesNotExist &) {
    // this is fine, we'll just attempt to create it
  }
  catch (const encfs::ConfigurationFileIsCorrupted &) {
    // this is not okay right now, let the user know and exit
    std::cout << "Configuration file is corrupted! Exiting..." << std::endl;
    return MAIN_RETURN_CODE_BAD_CONFIG_FILE;
  }

  opt::optional<encfs::SecureMem> maybe_password;
  if (!maybe_encfs_config) {
    // we have to create a configuration file
    // first let's check if the file system is in a good
    // state to allow it
    auto create_directory = false;
    try {
      auto is_dir = encfs::isDirectory(native_fs, encrypted_directory_path);
      if (!is_dir) {
        // this is not okay right now, let the user know and exit
        std::cout << "\"" << encrypted_directory_path << "\" is not a directory!" << std::endl;
        return MAIN_RETURN_CODE_ENCRYPTED_PATH_IS_NOT_A_DIRECTORY;
      }
    }
    catch (const std::system_error & err) {
      if (err.code() != std::errc::no_such_file_or_directory) throw;
      // TODO: check if we'll even be able to make the dir
      // using `userAllowAkdir`
      // directory doesn't exist, we'll have to create it
      create_directory = true;
    }

    std::cout << "Encrypted directory is not configured, " <<
      "proceeding to create configuration now..." << std::endl;
    if (create_directory) {
      std::cout << "Note: Directory \""<< encrypted_directory_path <<
        "\" will be created since it does not exist." << std::endl;
    }

    while (!maybe_password) {
      auto p1 = read_password_from_console("Enter new password");
      auto p2 = read_password_from_console("Verify new password");
      auto len1 = strlen((char *) p1.data());
      auto len2 = strlen((char *) p2.data());
      if (len1 == len2 &&
          !strcmp((char *) p1.data(), (char *) p2.data())) maybe_password = std::move(p1);
      else std::cout << "Passwords did not match, trying again..." << std::endl;
    }

    maybe_encfs_config = encfs::create_paranoid_config(*maybe_password);

    if (create_directory) native_fs->mkdir(encrypted_directory_path);
    encfs::write_config(native_fs, encrypted_directory_path,
                        *maybe_encfs_config);
  }
  else {
    // get password from console
    // repeat if user enters invalid password
    while (!maybe_password) {
      auto secure_password = read_password_from_console("Enter your password");

      const auto correct_password =
        encfs::verify_password(*maybe_encfs_config, secure_password);

      if (correct_password) maybe_password = std::move(secure_password);
      else std::cout << "Incorrect password! Try again" << std::endl;
    }
  }

  assert(maybe_encfs_config);
  assert(maybe_password);

  auto enc_fs = lockbox::create_enc_fs(std::move(native_fs),
                                       encrypted_directory_path,
                                       std::move(*maybe_encfs_config),
                                       std::move(*maybe_password));

  // do a simple test on encfs by writing a file and
  // reading the same file
  auto path = encrypted_directory_path.join("sup");

  auto test_string = std::string("SUP");

  auto f = enc_fs->openfile(path, true, true);
  f.write(0, test_string);

  auto data = f.read(0, 3);
  if (data != test_string) {
    std::cerr << "DATA WAS NOT EQUAL: \"" << data <<
      "\" VS \"" << test_string << "\"" << std::endl;
    return MAIN_RETURN_CODE_BAD_DATA;
  }

  return MAIN_RETURN_CODE_SUCCESS;
}
