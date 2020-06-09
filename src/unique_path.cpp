//  filesystem unique_path.cpp  --------------------------------------------------------//

//  Copyright Beman Dawes 2010
//  Copyright Andrey Semashev 2020

//  Distributed under the Boost Software License, Version 1.0.
//  See http://www.boost.org/LICENSE_1_0.txt

//  Library home page: http://www.boost.org/libs/filesystem

//--------------------------------------------------------------------------------------//

#ifndef BOOST_SYSTEM_NO_DEPRECATED
# define BOOST_SYSTEM_NO_DEPRECATED
#endif

// Include Boost.Predef first so that windows.h is guaranteed to be not included
#include <boost/predef/os/windows.h>
#include <boost/predef/os/cygwin.h>
#if BOOST_OS_WINDOWS || BOOST_OS_CYGWIN
#include <boost/winapi/config.hpp>
#endif

#include <boost/predef/library/c/cloudabi.h>
#include <boost/predef/os/bsd/open.h>
#include <boost/predef/os/bsd/free.h>
#include <boost/filesystem/config.hpp>

#ifdef BOOST_POSIX_API
#   include <cerrno>
#   include <stddef.h>
#   include <fcntl.h>
#   ifdef BOOST_HAS_UNISTD_H
#      include <unistd.h>
#   endif
#   if BOOST_OS_BSD_OPEN >= BOOST_VERSION_NUMBER(2, 1, 0) || BOOST_OS_BSD_FREE >= BOOST_VERSION_NUMBER(8, 0, 0) || BOOST_LIB_C_CLOUDABI
#      include <stdlib.h>
#      define BOOST_FILESYSTEM_HAS_ARC4RANDOM
#   endif
#   if (defined(__linux__) || defined(__linux) || defined(linux)) && (!defined(__ANDROID__) || __ANDROID_API__ >= 28)
#      include <sys/syscall.h>
#      if defined(SYS_getrandom)
#          define BOOST_FILESYSTEM_HAS_SYS_GETRANDOM
#      endif // defined(SYS_getrandom)
#      if defined(__has_include)
#          if __has_include(<sys/random.h>)
#              define BOOST_FILESYSTEM_HAS_GETRANDOM
#          endif
#      elif defined(__GLIBC__)
#          if __GLIBC_PREREQ(2, 25)
#              define BOOST_FILESYSTEM_HAS_GETRANDOM
#          endif
#      endif
#      if defined(BOOST_FILESYSTEM_HAS_GETRANDOM)
#          include <sys/random.h>
#      endif
#   endif // (defined(__linux__) || defined(__linux) || defined(linux)) && (!defined(__ANDROID__) || __ANDROID_API__ >= 28)
#else // BOOST_WINDOWS_API
#   include <boost/predef/platform.h>
#   include <boost/winapi/basic_types.hpp>
#   if defined(BOOST_FILESYSTEM_HAS_BCRYPT) // defined on the command line by the project
#      include <boost/winapi/error_codes.hpp>
#      include <boost/winapi/bcrypt.hpp>
#   else // defined(BOOST_FILESYSTEM_HAS_BCRYPT)
#      include <boost/winapi/crypt.hpp>
#      include <boost/winapi/get_last_error.hpp>
#   endif // defined(BOOST_FILESYSTEM_HAS_BCRYPT)
#endif

#include <cstddef>
#include <boost/filesystem/operations.hpp>
#include "error_handling.hpp"

namespace boost { namespace filesystem { namespace detail {

namespace {

#if defined(BOOST_FILESYSTEM_HAS_BCRYPT)
//! Converts NTSTATUS error codes to Win32 error codes for reporting
inline boost::winapi::DWORD_ translate_ntstatus(boost::winapi::NTSTATUS_ status)
{
  // Note: Legacy MinGW doesn't have ntstatus.h and doesn't define NTSTATUS error codes other than STATUS_SUCCESS.
  //       So define those here instead. Using the literals in the switch would trigger clangs Wc++11-narrowing
  BOOST_CONSTEXPR_OR_CONST NTSTATUS_ STATUS_NO_MEMORY_ = 0xC0000017l;
  BOOST_CONSTEXPR_OR_CONST NTSTATUS_ STATUS_INVALID_HANDLE_ = 0xC0000008l;
  BOOST_CONSTEXPR_OR_CONST NTSTATUS_ STATUS_INVALID_PARAMETER_ = 0xC000000Dl;
  switch (status)
  {
  case STATUS_NO_MEMORY_:
    return boost::winapi::ERROR_OUTOFMEMORY_;
  case STATUS_INVALID_HANDLE_:
    return boost::winapi::ERROR_INVALID_HANDLE_;
  case STATUS_INVALID_PARAMETER_:
    return boost::winapi::ERROR_INVALID_PARAMETER_;
  default:
    return boost::winapi::ERROR_NOT_SUPPORTED_;
  }
}
#endif // defined(BOOST_FILESYSTEM_HAS_BCRYPT)

void system_crypt_random(void* buf, std::size_t len, boost::system::error_code* ec)
{
#if defined(BOOST_POSIX_API)

#if defined(BOOST_FILESYSTEM_HAS_GETRANDOM) || defined(BOOST_FILESYSTEM_HAS_SYS_GETRANDOM)

  std::size_t bytes_read = 0;
  while (bytes_read < len)
  {
#if defined(BOOST_FILESYSTEM_HAS_GETRANDOM)
    ssize_t n = ::getrandom(buf, len - bytes_read, 0u);
#else
    ssize_t n = ::syscall(SYS_getrandom, buf, len - bytes_read, 0u);
#endif
    if (BOOST_UNLIKELY(n < 0))
    {
      int err = errno;
      if (err == EINTR)
        continue;
      emit_error(err, ec, "boost::filesystem::unique_path");
      return;
    }

    bytes_read += n;
    buf = static_cast<char*>(buf) + n;
  }

#elif defined(BOOST_FILESYSTEM_HAS_ARC4RANDOM)

  arc4random_buf(buf, len);

#else

  int file = open("/dev/urandom", O_RDONLY);
  if (file == -1)
  {
    file = open("/dev/random", O_RDONLY);
    if (file == -1)
    {
      emit_error(errno, ec, "boost::filesystem::unique_path");
      return;
    }
  }

  std::size_t bytes_read = 0;
  while (bytes_read < len)
  {
    ssize_t n = read(file, buf, len - bytes_read);
    if (BOOST_UNLIKELY(n == -1))
    {
      int err = errno;
      if (err == EINTR)
        continue;
      close(file);
      emit_error(err, ec, "boost::filesystem::unique_path");
      return;
    }
    bytes_read += n;
    buf = static_cast<char*>(buf) + n;
  }

  close(file);

#endif

#else // defined(BOOST_POSIX_API)

#if defined(BOOST_FILESYSTEM_HAS_BCRYPT)

  boost::winapi::BCRYPT_ALG_HANDLE_ handle;
  boost::winapi::NTSTATUS_ status = boost::winapi::BCryptOpenAlgorithmProvider(&handle, boost::winapi::BCRYPT_RNG_ALGORITHM_, NULL, 0);
  if (BOOST_UNLIKELY(status != 0))
  {
  fail:
    emit_error(translate_ntstatus(status), ec, "boost::filesystem::unique_path");
    return;
  }

  status = boost::winapi::BCryptGenRandom(handle, static_cast<boost::winapi::PUCHAR_>(buf), static_cast<boost::winapi::ULONG_>(len), 0);

  boost::winapi::BCryptCloseAlgorithmProvider(handle, 0);

  if (BOOST_UNLIKELY(status != 0))
    goto fail;

#else // defined(BOOST_FILESYSTEM_HAS_BCRYPT)

  boost::winapi::HCRYPTPROV_ handle;
  boost::winapi::DWORD_ err = 0u;
  if (BOOST_UNLIKELY(!boost::winapi::CryptAcquireContextW(&handle, NULL, NULL, boost::winapi::PROV_RSA_FULL_, boost::winapi::CRYPT_VERIFYCONTEXT_ | boost::winapi::CRYPT_SILENT_)))
  {
    err = boost::winapi::GetLastError();

  fail:
    emit_error(err, ec, "boost::filesystem::unique_path");
    return;
  }

  boost::winapi::BOOL_ gen_ok = boost::winapi::CryptGenRandom(handle, static_cast<boost::winapi::DWORD_>(len), static_cast<boost::winapi::BYTE_*>(buf));

  if (BOOST_UNLIKELY(!gen_ok))
    err = boost::winapi::GetLastError();

  boost::winapi::CryptReleaseContext(handle, 0);

  if (BOOST_UNLIKELY(!gen_ok))
    goto fail;

#endif // defined(BOOST_FILESYSTEM_HAS_BCRYPT)

#endif // defined(BOOST_POSIX_API)
}

#ifdef BOOST_WINDOWS_API
BOOST_CONSTEXPR_OR_CONST wchar_t hex[] = L"0123456789abcdef";
BOOST_CONSTEXPR_OR_CONST wchar_t percent = L'%';
#else
BOOST_CONSTEXPR_OR_CONST char hex[] = "0123456789abcdef";
BOOST_CONSTEXPR_OR_CONST char percent = '%';
#endif

}  // unnamed namespace

BOOST_FILESYSTEM_DECL
path unique_path(const path& model, system::error_code* ec)
{
  // This function used wstring for fear of misidentifying
  // a part of a multibyte character as a percent sign.
  // However, double byte encodings only have 80-FF as lead
  // bytes and 40-7F as trailing bytes, whereas % is 25.
  // So, use string on POSIX and avoid conversions.

  path::string_type s( model.native() );

  char ran[16] = {};  // init to avoid clang static analyzer message
                      // see ticket #8954
  BOOST_CONSTEXPR_OR_CONST unsigned int max_nibbles = 2u * sizeof(ran);   // 4-bits per nibble

  unsigned int nibbles_used = max_nibbles;
  for (path::string_type::size_type i = 0, n = s.size(); i < n; ++i)
  {
    if (s[i] == percent)                     // digit request
    {
      if (nibbles_used == max_nibbles)
      {
        system_crypt_random(ran, sizeof(ran), ec);
        if (ec != 0 && *ec)
          return path();
        nibbles_used = 0;
      }
      unsigned int c = ran[nibbles_used / 2u];
      c >>= 4u * (nibbles_used++ & 1u);  // if odd, shift right 1 nibble
      s[i] = hex[c & 0xf];               // convert to hex digit and replace
    }
  }

  if (ec != 0) ec->clear();

  return s;
}

}}}
