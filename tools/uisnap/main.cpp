// uisnap: plays a note (and, for an effect, two test pictures) through a plugin, then saves a picture of its
// editor window. A way to review the UI and the built-in scope without a DAW.
// usage: uisnap OUT.png [param=value ...]   (value as shown in the UI, e.g. wallMode=Drone)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: uisnap OUT.png [param=value ...]\n");
    return 2;
  }
  juce::ScopedJuceInitialiser_GUI gui;
  int status = 0;
  {
    std::unique_ptr<juce::AudioProcessor> plugin(createPluginFilter());
    for (int i = 2; i < argc; ++i) {
      const juce::String arg(argv[i]);
      const juce::String id = arg.upToFirstOccurrenceOf("=", false, false), value = arg.fromFirstOccurrenceOf("=", false, false);
      bool found = false;
      for (auto* p : plugin->getParameters()) {
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p); withId && withId->paramID == id) {
          p->setValueNotifyingHost(p->getValueForText(value));
          found = true;
        }
      }
      if (!found) {
        std::fprintf(stderr, "uisnap: no parameter '%s'\n", id.toRawUTF8());
        return 2;
      }
    }

    const double sampleRate = 48000.0;
    const int block = 128;
    plugin->setRateAndBufferSizeDetails(sampleRate, block); // buses stay as the plugin declares them
    plugin->prepareToPlay(sampleRate, block);
    std::unique_ptr<juce::AudioProcessorEditor> editor(plugin->createEditorIfNeeded());
    editor->setVisible(true);

    // Half a second of a held note (A3, velocity 80), paced so the scope's
    // timer sees it arrive as it would live.
    juce::AudioBuffer<float> buffer(juce::jmax(2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels()), block);
    for (int b = 0; b < static_cast<int>(0.5 * sampleRate / block); ++b) {
      juce::MidiBuffer midi;
      if (b == 0)
        midi.addEvent(juce::MidiMessage::noteOn(1, 57, static_cast<juce::uint8>(80)), 0);
      buffer.clear();
      if (plugin->getTotalNumInputChannels() >= 2) {
        // An effect: a circle at 55 Hz on the main input, a square at 220 Hz on the sidechain.
        for (int i = 0; i < block; ++i) {
          const double t = (static_cast<double>(b) * block + i) / sampleRate;
          buffer.setSample(0, i, static_cast<float>(0.8 * std::cos(juce::MathConstants<double>::twoPi * 55.0 * t)));
          buffer.setSample(1, i, static_cast<float>(0.8 * std::sin(juce::MathConstants<double>::twoPi * 55.0 * t)));
          if (buffer.getNumChannels() >= 4) {
            const double u = (220.0 * t - std::floor(220.0 * t)) * 4.0;
            const int side = static_cast<int>(u);
            const double w = (u - side) * 2.0 - 1.0;
            const double px[] = {w, 1.0, -w, -1.0}, py[] = {-1.0, w, 1.0, -w};
            buffer.setSample(2, i, static_cast<float>(0.3 * px[side]));
            buffer.setSample(3, i, static_cast<float>(0.3 * py[side]));
          }
        }
      }
      plugin->processBlock(buffer, midi);
      if (b % 6 == 5)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(16);
    }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    const juce::Image image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 2.0f);
    juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    out.deleteFile();
    juce::FileOutputStream stream(out);
    if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream)) {
      std::fprintf(stderr, "uisnap: cannot write %s\n", out.getFullPathName().toRawUTF8());
      status = 1;
    } else {
      std::printf("%s: %d x %d\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
    }
    editor.reset();
    plugin->releaseResources();
  }
  return status;
}
