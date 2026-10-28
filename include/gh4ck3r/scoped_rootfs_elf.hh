#pragma once
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <vector>
#include <climits>
#include <cstring>
#include <elf.h>

namespace gh4ck3r::filesystem::scoped_rootfs::elf {
namespace fs = std::filesystem;
using path_t = fs::path;

struct RuntimeElfInfo {
  path_t interpreter;
  std::string soname;
};

template <typename Header, typename ProgramHeader, typename Dynamic>
inline RuntimeElfInfo runtime_elf_info(std::ifstream &input) {
  RuntimeElfInfo result;
  Header header {};
  input.clear();
  input.seekg(0);
  input.read(reinterpret_cast<char *>(&header), sizeof(header));
  if (!input || header.e_phentsize != sizeof(ProgramHeader))
    throw std::runtime_error {"invalid executable program headers"};
  std::vector<ProgramHeader> programs;
  for (unsigned int i = 0; i < header.e_phnum; ++i) {
    ProgramHeader program {};
    input.seekg(header.e_phoff + i * header.e_phentsize);
    input.read(reinterpret_cast<char *>(&program), sizeof(program));
    if (!input) throw std::runtime_error {"read executable program header"};
    programs.push_back(program);
    if (program.p_type != PT_INTERP) continue;
    if (program.p_filesz < 2 || program.p_filesz > PATH_MAX)
      throw std::runtime_error {"invalid executable interpreter"};
    std::string path(program.p_filesz, '\0');
    input.seekg(program.p_offset);
    input.read(path.data(), path.size());
    if (!input || path.back() != '\0')
      throw std::runtime_error {"read executable interpreter"};
    path.pop_back();
    result.interpreter = path;
  }

  std::uint64_t strings = 0;
  std::uint64_t strings_size = 0;
  std::optional<std::uint64_t> soname;
  for (const auto &program : programs) {
    if (program.p_type != PT_DYNAMIC) continue;
    for (std::uint64_t offset = 0; offset + sizeof(Dynamic) <= program.p_filesz;
         offset += sizeof(Dynamic)) {
      Dynamic entry {};
      input.seekg(program.p_offset + offset);
      input.read(reinterpret_cast<char *>(&entry), sizeof(entry));
      if (!input) throw std::runtime_error {"read executable dynamic entry"};
      if (entry.d_tag == DT_NULL) break;
      if (entry.d_tag == DT_STRTAB) strings = entry.d_un.d_ptr;
      if (entry.d_tag == DT_STRSZ) strings_size = entry.d_un.d_val;
      if (entry.d_tag == DT_SONAME) soname = entry.d_un.d_val;
    }
  }
  if (!soname) return result;
  if (*soname >= strings_size || *soname > UINT64_MAX - strings)
    throw std::runtime_error {"invalid runtime library name offset"};
  const auto address = strings + *soname;
  for (const auto &program : programs) {
    if (program.p_type != PT_LOAD || address < program.p_vaddr ||
        address - program.p_vaddr >= program.p_filesz) continue;
    const auto offset = address - program.p_vaddr;
    const auto available = std::min<std::uint64_t>(
        strings_size - *soname, program.p_filesz - offset);
    input.seekg(program.p_offset + offset);
    for (std::uint64_t i = 0; i < available && i < PATH_MAX; ++i) {
      char ch;
      if (!input.get(ch)) throw std::runtime_error {"read runtime library name"};
      if (ch == '\0') {
        if (result.soname.empty() || result.soname == "." || result.soname == ".." ||
            result.soname.find('/') != std::string::npos)
          throw std::runtime_error {"invalid runtime library name"};
        return result;
      }
      result.soname += ch;
    }
    throw std::runtime_error {"unterminated runtime library name"};
  }
  throw std::runtime_error {"runtime library name is outside its file segments"};
}

inline RuntimeElfInfo runtime_elf_info(const path_t &path, bool require_elf = false) {
  std::ifstream input {path, std::ios::binary};
  if (!input) throw std::runtime_error {"open runtime file: " + path.string()};
  std::array<unsigned char, EI_NIDENT> ident {};
  input.read(reinterpret_cast<char *>(ident.data()), ident.size());
  if (input.gcount() < SELFMAG || std::memcmp(ident.data(), ELFMAG, SELFMAG) != 0) {
    if (require_elf) throw std::runtime_error {"invalid executable ELF header"};
    return {};
  }
  if (!input) throw std::runtime_error {"incomplete runtime ELF header"};
  if (ident[EI_CLASS] == ELFCLASS64)
    return runtime_elf_info<Elf64_Ehdr, Elf64_Phdr, Elf64_Dyn>(input);
  if (ident[EI_CLASS] == ELFCLASS32)
    return runtime_elf_info<Elf32_Ehdr, Elf32_Phdr, Elf32_Dyn>(input);
  throw std::runtime_error {"unsupported runtime ELF class"};
}

} // namespace gh4ck3r::filesystem::scoped_rootfs::elf
