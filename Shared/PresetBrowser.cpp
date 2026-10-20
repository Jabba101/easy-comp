#include "PresetBrowser.h"
#include "Look.h"

PresetBrowser::PresetBrowser()
{
    // The box you type a name into when saving (hidden until then)
    nameEditor.setTextToShowWhenEmpty ("Type a name, then press Enter", Palette::textDim);
    nameEditor.setFont (juce::Font (juce::FontOptions (15.0f)));
    nameEditor.setJustification (juce::Justification::centredLeft);
    nameEditor.setIndents (10, 0);
    nameEditor.onReturnKey = [this] { finishNaming (true); };
    nameEditor.onEscapeKey = [this] { finishNaming (false); };
    nameEditor.onFocusLost = [this] { finishNaming (false); };
    addChildComponent (nameEditor);

    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setRepaintsOnMouseActivity (true);   // the outline brightens under the mouse
}

void PresetBrowser::setPresets (const juce::String& newPluginName, std::vector<Preset> builtInPresets)
{
    pluginName = newPluginName;
    builtIn    = std::move (builtInPresets);
}

void PresetBrowser::showCurrent (const juce::String& presetName, bool edited)
{
    if (presetName == shownName && edited == shownEdited)
        return;

    shownName   = presetName;
    shownEdited = edited;
    repaint();
}

const Preset* PresetBrowser::findPreset (const juce::String& name) const
{
    for (auto* list : { &builtIn, &user })
        for (auto& preset : *list)
            if (preset.name == name)
                return &preset;

    return nullptr;
}

//==============================================================================
// Your own presets
//==============================================================================
juce::File PresetBrowser::getUserPresetFolder() const
{
    if (userFolderOverride != juce::File())
        return userFolderOverride;

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("Easy Plugins")
               .getChildFile (pluginName);
}

std::vector<Preset> PresetBrowser::readUserPresets() const
{
    std::vector<Preset> found;

    for (auto& file : getUserPresetFolder().findChildFiles (juce::File::findFiles, false, "*.xml"))
    {
        const auto xml = juce::parseXML (file);

        if (xml == nullptr || ! xml->hasTagName ("EasyPreset") || xml->getStringAttribute ("plugin") != pluginName)
            continue;

        Preset preset;
        preset.category = "My presets";
        preset.name     = xml->getStringAttribute ("name", file.getFileNameWithoutExtension());
        preset.tip      = "One of your own presets (saved in Documents > Easy Plugins > " + pluginName + ").";

        for (auto* setting : xml->getChildWithTagNameIterator ("PARAM"))
            preset.values.emplace_back (setting->getStringAttribute ("id"), (float) setting->getDoubleAttribute ("value"));

        found.push_back (std::move (preset));
    }

    std::sort (found.begin(), found.end(),
               [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return found;
}

juce::String PresetBrowser::saveUserPreset (const juce::String& name)
{
    auto cleanName = name.trim();

    if (cleanName.isEmpty() || getCurrentValues == nullptr)
        return {};

    // Never shadow a built-in preset: keep both, clearly named.
    for (auto& preset : builtIn)
        if (preset.name.equalsIgnoreCase (cleanName))
            cleanName << " (mine)";

    auto folder = getUserPresetFolder();

    if (! folder.createDirectory())
        return {};

    juce::XmlElement xml ("EasyPreset");
    xml.setAttribute ("plugin", pluginName);
    xml.setAttribute ("name", cleanName);

    for (auto& [id, value] : getCurrentValues())
    {
        auto* setting = xml.createNewChildElement ("PARAM");
        setting->setAttribute ("id", id);
        setting->setAttribute ("value", value);
    }

    const auto file = folder.getChildFile (juce::File::createLegalFileName (cleanName) + ".xml");

    if (! xml.writeTo (file))
        return {};

    user = readUserPresets();
    return cleanName;
}

//==============================================================================
// The menu
//==============================================================================
juce::PopupMenu PresetBrowser::buildMenu()
{
    user = readUserPresets();   // pick up anything saved since last time

    juce::PopupMenu menu;
    juce::StringArray categories;

    for (auto& preset : builtIn)
        if (preset.category.isNotEmpty())
            categories.addIfNotAlreadyThere (preset.category);   // in the order they're listed

    // Presets with no category sit at the top (e.g. "Default settings")
    for (size_t i = 0; i < builtIn.size(); ++i)
        if (builtIn[i].category.isEmpty())
            menu.addItem (firstBuiltIn + (int) i, builtIn[i].name, true, builtIn[i].name == shownName);

    if (menu.getNumItems() > 0)
        menu.addSeparator();

    for (auto& category : categories)
    {
        juce::PopupMenu sub;
        bool containsCurrent = false;

        for (size_t i = 0; i < builtIn.size(); ++i)
        {
            if (builtIn[i].category == category)
            {
                const bool current = builtIn[i].name == shownName;
                sub.addItem (firstBuiltIn + (int) i, builtIn[i].name, true, current);
                containsCurrent = containsCurrent || current;
            }
        }

        menu.addSubMenu (category, sub, true, nullptr, containsCurrent);
    }

    menu.addSeparator();

    juce::PopupMenu mine;
    bool containsCurrent = false;

    for (size_t i = 0; i < user.size(); ++i)
    {
        const bool current = user[i].name == shownName;
        mine.addItem (firstUser + (int) i, user[i].name, true, current);
        containsCurrent = containsCurrent || current;
    }

    if (user.empty())
        menu.addItem (firstUser - 1, "My presets (none saved yet)", false);
    else
        menu.addSubMenu ("My presets", mine, true, nullptr, containsCurrent);

    menu.addItem (saveItem, "Save current settings as a preset...");
    menu.addItem (openFolderItem, "Open my presets folder");
    return menu;
}

void PresetBrowser::handleMenuResult (int itemId)
{
    if (itemId >= firstBuiltIn && itemId < firstBuiltIn + (int) builtIn.size())
    {
        if (onPresetChosen != nullptr)
            onPresetChosen (builtIn[(size_t) (itemId - firstBuiltIn)]);
    }
    else if (itemId >= firstUser && itemId < firstUser + (int) user.size())
    {
        if (onPresetChosen != nullptr)
            onPresetChosen (user[(size_t) (itemId - firstUser)]);
    }
    else if (itemId == saveItem)
    {
        startNaming();
    }
    else if (itemId == openFolderItem)
    {
        auto folder = getUserPresetFolder();
        folder.createDirectory();
        folder.startAsProcess();
    }
}

void PresetBrowser::mouseDown (const juce::MouseEvent&)
{
    if (isNaming())
        return;

    juce::Component::SafePointer<PresetBrowser> safeThis (this);

    buildMenu().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                         .withMinimumWidth (getWidth())
                                                         .withStandardItemHeight (26),
                               [safeThis] (int result)
                               {
                                   if (safeThis != nullptr && result != 0)
                                       safeThis->handleMenuResult (result);
                               });
}

//==============================================================================
// Naming a preset when saving
//==============================================================================
void PresetBrowser::startNaming()
{
    // Suggest the current preset's name (without "(edited)") as a starting point
    nameEditor.setText (shownName, juce::dontSendNotification);
    nameEditor.selectAll();
    nameEditor.setVisible (true);

    if (nameEditor.isShowing())   // (it always is in a DAW; it can only take typing when on screen)
        nameEditor.grabKeyboardFocus();

    repaint();
}

void PresetBrowser::finishNaming (bool save)
{
    if (! isNaming())
        return;

    const auto name = nameEditor.getText().trim();
    nameEditor.setVisible (false);
    repaint();

    if (! save)
        return;

    if (const auto savedAs = saveUserPreset (name); savedAs.isNotEmpty() && onPresetSaved != nullptr)
        onPresetSaved (savedAs);
}

//==============================================================================
void PresetBrowser::resized()
{
    nameEditor.setBounds (getLocalBounds());
}

void PresetBrowser::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (0.5f);

    g.setColour (Palette::panel);
    g.fillRoundedRectangle (bounds, 7.0f);
    g.setColour (isMouseOver (true) ? Palette::textDim : Palette::panelEdge);
    g.drawRoundedRectangle (bounds, 7.0f, 1.0f);

    if (isNaming())
        return;

    auto area = getLocalBounds().reduced (12, 0);

    // The little arrow on the right
    const auto arrowArea = area.removeFromRight (14).toFloat().withSizeKeepingCentre (12.0f, 7.0f);
    juce::Path arrow;
    arrow.startNewSubPath (arrowArea.getTopLeft());
    arrow.lineTo (arrowArea.getCentreX(), arrowArea.getBottom());
    arrow.lineTo (arrowArea.getTopRight());
    g.setColour (Palette::accent);
    g.strokePath (arrow, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    area.removeFromRight (8);

    g.setFont (juce::Font (juce::FontOptions (15.0f)));

    if (shownName.isEmpty())
    {
        g.setColour (Palette::textDim);
        g.drawText ("Presets...", area, juce::Justification::centredLeft, true);
    }
    else
    {
        g.setColour (Palette::textBright);
        g.drawFittedText (shownName + (shownEdited ? " (edited)" : ""), area, juce::Justification::centredLeft, 1, 0.85f);
    }
}
