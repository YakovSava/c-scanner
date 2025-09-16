#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <stack>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif
#include <filesystem>

namespace fs = std::filesystem;

struct Args {
  std::string path = ".";
  std::optional<std::string> out;
};

static inline std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

static inline bool StartsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static inline bool EndsWith(std::string_view s, std::string_view suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

static inline std::string Trim(std::string s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
  size_t j = s.size();
  while (j > i && (s[j - 1] == ' ' || s[j - 1] == '\t' || s[j - 1] == '\r' || s[j - 1] == '\n')) --j;
  return s.substr(i, j - i);
}

static inline std::string PathToPosix(const fs::path& p) {
  return p.generic_string();
}

static inline std::string NormalizeSlashes(std::string s) {
  for (char& c : s) if (c == '\\') c = '/';
  return s;
}

static Args ParseArguments(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "-p" || a == "--path") {
      if (i + 1 < argc) {
        args.path = argv[++i];
      } else {
        std::cerr << "Error: missing value for " << a << "\n";
        std::exit(1);
      }
    } else if (a == "--out") {
      if (i + 1 < argc) {
        args.out = argv[++i];
      } else {
        std::cerr << "Error: missing value for --out\n";
        std::exit(1);
      }
    } else {
      std::cerr << "Unknown argument: " << a << "\n";
      std::exit(1);
    }
  }
  return args;
}

struct Pattern {
  std::string pattern;
  bool negated = false;
  bool dir_only = false;
  bool anchored = false;
};

static std::vector<std::string> Split(const std::string& s, char delim) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == delim) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur.push_back(c);
    }
  }
  out.push_back(cur);
  return out;
}

static bool SegmentMatch(const std::string& pat, const std::string& text) {
  size_t pi = 0, ti = 0;
  size_t star_pi = std::string::npos, star_ti = std::string::npos;
  while (ti < text.size()) {
    if (pi < pat.size() && (pat[pi] == '?' || pat[pi] == text[ti])) {
      ++pi; ++ti;
    } else if (pi < pat.size() && pat[pi] == '*') {
      star_pi = ++pi;
      star_ti = ti;
    } else if (star_pi != std::string::npos) {
      pi = star_pi;
      ti = ++star_ti;
    } else {
      return false;
    }
  }
  while (pi < pat.size() && pat[pi] == '*') ++pi;
  return pi == pat.size();
}

static bool TokensMatch(const std::vector<std::string>& ptokens,
                        const std::vector<std::string>& stokens,
                        size_t pi, size_t si) {
  if (pi == ptokens.size()) return si == stokens.size();
  if (ptokens[pi] == "**") {
    for (size_t k = si; k <= stokens.size(); ++k) {
      if (TokensMatch(ptokens, stokens, pi + 1, k)) return true;
    }
    return false;
  }
  if (si == stokens.size()) return false;
  if (!SegmentMatch(ptokens[pi], stokens[si])) return false;
  return TokensMatch(ptokens, stokens, pi + 1, si + 1);
}

static bool WildMatchPath(const std::string& pattern, const std::string& path) {
  auto ptokens = Split(pattern, '/');
  auto stokens = Split(path, '/');
  return TokensMatch(ptokens, stokens, 0, 0);
}

static bool GitWildMatch(const Pattern& p, const std::string& rel_path, bool is_dir) {
  if (p.dir_only && !is_dir) return false;
  std::string path = rel_path;
  if (EndsWith(path, "/")) {
    if (!p.dir_only) path.pop_back();
  }
  if (p.anchored) {
    return WildMatchPath(p.pattern, path);
  }
  if (WildMatchPath(p.pattern, path)) return true;
  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '/' && i + 1 < path.size()) {
      if (WildMatchPath(p.pattern, path.substr(i + 1))) return true;
    }
  }
  return false;
}

static std::vector<Pattern> LoadGitignoreSpec(const fs::path& root) {
  std::vector<Pattern> res;
  fs::path gitignore = root / ".gitignore";
  if (!fs::exists(gitignore) || !fs::is_regular_file(gitignore)) return res;
  std::ifstream in(gitignore);
  if (!in) return res;
  std::string line;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty()) continue;
    if (!line.empty() && line[0] == '#') continue;
    bool neg = false;
    if (!line.empty() && line[0] == '!') {
      neg = true;
      line.erase(line.begin());
      line = Trim(line);
      if (line.empty()) continue;
    }
    bool dir_only = false;
    if (EndsWith(line, "/")) {
      dir_only = true;
      line.pop_back();
    }
    bool anchored = false;
    if (StartsWith(line, "/")) {
      anchored = true;
      while (!line.empty() && line[0] == '/') line.erase(line.begin());
    }
    line = NormalizeSlashes(line);
    if (line == "**") dir_only = false;
    res.push_back(Pattern{line, neg, dir_only, anchored});
  }
  return res;
}

static bool IsIgnored(const std::vector<Pattern>& patterns,
                      const std::string& rel_posix, bool is_dir) {
  bool matched = false;
  for (const auto& p : patterns) {
    if (GitWildMatch(p, rel_posix, is_dir)) {
      matched = !p.negated;
    }
  }
  return matched;
}

static void PrintFile(std::ostream& out, const fs::path& p) {
  out << fs::weakly_canonical(p).string() << "\n";
  out << "```" << "\n";
  std::ifstream in(p, std::ios::in | std::ios::binary);
  std::string buf;
  if (in) {
    in.seekg(0, std::ios::end);
    std::streampos len = in.tellg();
    in.seekg(0, std::ios::beg);
    buf.resize(static_cast<size_t>(std::max<std::streampos>(0, len)));
    if (!buf.empty()) in.read(&buf[0], static_cast<std::streamsize>(buf.size()));
  }
  out << buf;
  if (!buf.empty() && buf.back() != '\n') out << "\n";
  out << "```" << "\n\n";
}

static void PrintError(std::ostream& out, const std::string& msg) {
  std::cerr << msg << "\n";
  out << msg << "\n\n";
}

static void FindAndPrintFiles(const fs::path& root, std::ostream& out, const fs::path& self_path) {
  auto patterns = LoadGitignoreSpec(root);
  std::error_code ec;
  fs::path root_abs = fs::weakly_canonical(root, ec);
  if (ec) root_abs = root;

  std::stack<fs::path> dirs;
  dirs.push(root_abs);

  while (!dirs.empty()) {
    fs::path cur = dirs.top();
    dirs.pop();

    for (auto it = fs::directory_iterator(cur, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::end(it); it.increment(ec)) {
      if (ec) break;
      const fs::directory_entry& entry = *it;
      fs::path p = entry.path();
      fs::path rel = fs::relative(p, root_abs, ec);
      if (ec) rel = p.lexically_relative(root_abs);
      std::string rel_posix = PathToPosix(rel);

      bool is_dir = entry.is_directory(ec) && !ec;
      bool is_reg = entry.is_regular_file(ec) && !ec;

      if (IsIgnored(patterns, rel_posix, is_dir)) {
        if (is_dir) continue;
        if (!is_reg) continue;
        if (is_reg) continue;
      }

      if (is_dir) {
        dirs.push(p);
        continue;
      }

      if (!is_reg) continue;

      fs::path can_p = fs::weakly_canonical(p, ec);
      fs::path can_self = fs::weakly_canonical(self_path, ec);
      if (!ec && !can_self.empty() && can_p == can_self) continue;

      std::ifstream in(p, std::ios::in | std::ios::binary);
      if (!in) {
        PrintError(out, std::string("Failed to read file ") + p.string() + ": open error");
        continue;
      }
      in.close();
      PrintFile(out, p);
    }
  }
}

int main(int argc, char** argv) {
#if defined(_WIN32)
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif

  Args args = ParseArguments(argc, argv);
  fs::path start_directory = args.path;
  std::error_code ec;
  if (!fs::exists(start_directory, ec) || !fs::is_directory(start_directory, ec)) {
    std::cerr << "Error: directory '" << start_directory.string() << "' not found.\n";
    return 1;
  }

  fs::path self_path;
  try {
    self_path = fs::absolute(argv[0]);
  } catch (...) {
    self_path = argv[0];
  }

  if (args.out.has_value()) {
    std::ofstream outfile(args.out.value(), std::ios::out | std::ios::binary);
    if (!outfile) {
      std::cerr << "Error writing to '" << args.out.value() << "': unable to open file\n";
      return 1;
    }
    FindAndPrintFiles(start_directory, outfile, self_path);
    std::cout << "Output successfully written to: " << args.out.value() << "\n";
  } else {
#if defined(_WIN32)
    std::setlocale(LC_ALL, ".UTF-8");
#endif
    FindAndPrintFiles(start_directory, std::cout, self_path);
  }
  return 0;
}
