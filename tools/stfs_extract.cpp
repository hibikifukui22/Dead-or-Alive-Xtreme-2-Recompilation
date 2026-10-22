#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <span>
#include <vector>

#include <rex/filesystem/devices/stfs_container_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

namespace {

bool ExtractFile(rex::filesystem::Entry& entry, const std::filesystem::path& output_path) {
  rex::filesystem::File* input = nullptr;
  if (entry.Open(rex::filesystem::FileAccess::kFileReadData, &input) != rex::X_STATUS{0}) {
    std::cerr << "Could not open package entry: " << entry.path() << '\n';
    return false;
  }

  std::vector<uint8_t> bytes(entry.size());
  size_t read = 0;
  const auto status = input->ReadSync(bytes, 0, &read);
  input->Destroy();
  if (status != rex::X_STATUS{0} || read != bytes.size()) {
    std::cerr << "Could not read package entry: " << entry.path() << '\n';
    return false;
  }

  std::filesystem::create_directories(output_path.parent_path());
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: stfs_extract <package> <output-directory>\n";
    return 2;
  }

  const std::filesystem::path package_path = argv[1];
  const std::filesystem::path output_dir = argv[2];
  rex::filesystem::StfsContainerDevice package("", package_path);
  if (!package.Initialize()) {
    std::cerr << "Could not open STFS package.\n";
    return 1;
  }

  auto* root = package.ResolvePath("");
  if (!root) {
    std::cerr << "Package has no root directory.\n";
    return 1;
  }

  bool ok = true;
  std::queue<rex::filesystem::Entry*> queue;
  queue.push(root);
  while (!queue.empty()) {
    auto* entry = queue.front();
    queue.pop();
    for (const auto& child : entry->children()) {
      queue.push(child.get());
    }
    if (entry == root || (entry->attributes() & rex::filesystem::kFileAttributeDirectory)) {
      continue;
    }

    const auto name = std::filesystem::path(entry->path()).filename();
    const auto destination = output_dir / name;
    std::cout << entry->path() << " -> " << destination.string() << '\n';
    ok = ExtractFile(*entry, destination) && ok;
  }
  return ok ? 0 : 1;
}
