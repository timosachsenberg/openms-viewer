// Headless capture of the 3-D peak surface, for verifying the QRhiWidget renderer.
// Usage: surface3d_shot [input.mzML] [out.png]
#include "widgets/PeakSurface3DWidget.h"
#include "plot/PlotRange.h"

#include <OpenMS/FORMAT/MzMLFile.h>
#include <OpenMS/KERNEL/MSExperiment.h>

#include <QApplication>
#include <QImage>
#include <QTimer>

#include <algorithm>
#include <iostream>
#include <memory>

using namespace OpenMSViewer;

int main(int argc, char** argv)
{
  QApplication app(argc, argv);
  const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                : QStringLiteral("tests/data/BSA1_F1.mzML");
  const QString out = argc > 2 ? QString::fromLocal8Bit(argv[2])
                               : QStringLiteral("/tmp/surface3d.png");

  auto experiment = std::make_shared<OpenMS::MSExperiment>();
  OpenMS::MzMLFile().load(path.toStdString(), *experiment);
  experiment->updateRanges();
  experiment->sortSpectra();

  // Data bounds + the strongest MS1 peak, to centre a window that fits the 3-D view.
  double rtMin = 1e18, rtMax = -1e18, mzMin = 1e18, mzMax = -1e18;
  double bestInt = -1.0, bestRt = 0.0, bestMz = 0.0;
  for (const auto& spectrum : *experiment)
  {
    if (spectrum.getMSLevel() != 1) continue;
    const double rt = spectrum.getRT();
    rtMin = std::min(rtMin, rt); rtMax = std::max(rtMax, rt);
    for (const auto& peak : spectrum)
    {
      const double mz = peak.getMZ(), in = peak.getIntensity();
      mzMin = std::min(mzMin, mz); mzMax = std::max(mzMax, mz);
      if (in > bestInt) { bestInt = in; bestRt = rt; bestMz = mz; }
    }
  }
  const double rtHalf = std::min(30.0, (rtMax - rtMin) / 2.0);
  const double mzHalf = std::min(10.0, (mzMax - mzMin) / 2.0);
  const PlotRange range{ std::max(rtMin, bestRt - rtHalf), std::min(rtMax, bestRt + rtHalf),
                         std::max(mzMin, bestMz - mzHalf), std::min(mzMax, bestMz + mzHalf) };
  std::cerr << "data rt[" << rtMin << "," << rtMax << "] mz[" << mzMin << "," << mzMax << "]\n"
            << "window rt[" << range.rtMin << "," << range.rtMax << "] mz["
            << range.mzMin << "," << range.mzMax << "] peakInt=" << bestInt << "\n";

  auto* widget = new PeakSurface3DWidget();
  widget->resize(760, 560);
  widget->show();
  widget->setView(experiment, range, PeakMapColorMap::Plasma, 1,
                  PeakMapIntensityScale::Equalized);

  // Let the async grid compute and a few frames render, then grab the framebuffer.
  QTimer::singleShot(3000, &app, [&]
  {
    const QImage image = widget->grabFramebuffer();
    if (image.isNull())
    {
      std::cerr << "grabFramebuffer() returned a null image (no GPU context?)\n";
      app.exit(2);
      return;
    }
    if (image.save(out))
      std::cerr << "saved " << out.toStdString() << " " << image.width() << "x"
                << image.height() << "\n";
    else
      std::cerr << "failed to save " << out.toStdString() << "\n";
    app.quit();
  });
  return app.exec();
}
