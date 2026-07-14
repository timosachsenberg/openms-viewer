#include <OpenMS/config.h>

#ifdef WITH_THERMO_RAW
#include <OpenMS/FORMAT/ThermoRawFile.h>
#include <OpenMS/KERNEL/MSExperiment.h>
#endif

#include <cstdint>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
  if (argc != 2)
  {
    std::cerr << "Usage: openms-viewer-thermo-smoke <file.raw>\n";
    return 2;
  }

#ifdef WITH_THERMO_RAW
  try
  {
    OpenMS::MSExperiment experiment;
    OpenMS::ThermoRawFile reader;
    reader.setLogType(OpenMS::ProgressLogger::NONE);
    reader.load(argv[1], experiment);

    std::uint64_t peak_count = 0;
    for (const auto& spectrum : experiment.getSpectra())
    {
      peak_count += spectrum.size();
    }

    if (experiment.size() != 32)
    {
      std::cerr << "Expected 32 spectra, loaded " << experiment.size() << ".\n";
      return 3;
    }
    if (peak_count == 0)
    {
      std::cerr << "Thermo RAW reader returned no peaks.\n";
      return 4;
    }
    if (experiment.getSourceFiles().empty())
    {
      std::cerr << "Thermo RAW reader returned no source-file metadata.\n";
      return 5;
    }

    std::cout << "Loaded " << experiment.size() << " Thermo RAW spectra with "
              << peak_count << " peaks.\n";
    return 0;
  }
  catch (const std::exception& error)
  {
    std::cerr << "Thermo RAW smoke test failed: " << error.what() << '\n';
    return 6;
  }
#else
  std::cerr << "OpenMS was built without Thermo RAW support.\n";
  return 7;
#endif
}
