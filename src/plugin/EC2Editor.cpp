#include "EC2Editor.h"

#include "EC2Plugin.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "IControls.h"
#include "IGraphicsPopupMenu.h"

using namespace iplug;
using namespace iplug::igraphics;

namespace {

struct Palette
{
  IColor primary;
  IColor shade1;
  IColor shade2;
  IColor shade3;
  IColor blue;
  IColor green;
  IColor yellow;
  IColor red;
  IColor text;
  IColor muted;
  IColor black;
  IColor hover;
  IColor popup;
  IColor popupSelected;
  IColor histogram;
};

const Palette kDark{
  {255, 33, 38, 40}, {255, 55, 63, 66}, {255, 76, 88, 92}, {255, 98, 113, 118},
  {255, 15, 75, 129}, {255, 8, 159, 8}, {255, 122, 114, 0}, {255, 170, 8, 76},
  {255, 255, 255, 255}, {255, 190, 198, 201}, {255, 0, 0, 0}, {35, 255, 255, 255},
  {255, 42, 48, 50}, {255, 15, 75, 129}, {150, 255, 255, 255}};

const Palette kLight{
  {255, 149, 176, 176}, {255, 176, 196, 196}, {255, 199, 213, 213}, {255, 221, 230, 230},
  {255, 44, 113, 175}, {255, 69, 201, 69}, {255, 237, 224, 39}, {255, 212, 35, 89},
  {255, 0, 0, 0}, {255, 55, 68, 70}, {255, 0, 0, 0}, {35, 0, 0, 0},
  {255, 221, 230, 230}, {255, 44, 113, 175}, {150, 0, 0, 0}};

const Palette kStudio{
  {255, 33, 33, 36}, {255, 45, 45, 48}, {255, 58, 58, 62}, {255, 72, 72, 77},
  {255, 140, 194, 70}, {255, 140, 194, 70}, {255, 245, 140, 45}, {255, 245, 100, 30},
  {255, 238, 238, 238}, {255, 165, 165, 168}, {255, 10, 10, 12}, {35, 245, 130, 32},
  {255, 42, 42, 45}, {200, 245, 130, 32}, {180, 140, 194, 70}};

const Palette& C(const EC2Next* plug)
{
  const int skin = plug ? plug->editorSkin() : 0;
  if (skin == 1) return kLight;
  if (skin == 2) return kStudio;
  return kDark;
}

IText Text(float size, EAlign align, const IColor& color)
{
  return IText(size, color, "Roboto", align);
}

IText Text(const EC2Next* plug, float size, EAlign align = EAlign::Center)
{
  return Text(size, align, C(plug).text);
}

IText Text(const EC2Next* plug, float size, EAlign align, const IColor& color)
{
  return Text(size, align, color);
}

double Clamp(double value, double lo, double hi)
{
  return std::max(lo, std::min(hi, value));
}

const IColor& Shade(const EC2Next* plug, int index)
{
  if (index % 3 == 0) return C(plug).shade3;
  if (index % 3 == 1) return C(plug).shade2;
  return C(plug).shade1;
}

const IColor& Accent(const EC2Next* plug, int index)
{
  if (index < 4) return C(plug).blue;
  if (index < 7) return C(plug).green;
  if (index < 11) return C(plug).yellow;
  return C(plug).red;
}

void StylePopup(EC2Next* plug)
{
  if (!plug || !plug->GetUI() || !plug->GetUI()->GetPopupMenuControl()) return;
  auto* popup = plug->GetUI()->GetPopupMenuControl();
  popup->SetPanelColor(C(plug).popup);
  popup->SetCellBackgroundColor(C(plug).popupSelected);
  popup->SetItemMouseoverColor(kDark.text);
  popup->SetItemColor(C(plug).text);
  popup->SetDisabledItemColor(C(plug).muted);
  popup->SetSeparatorColor(C(plug).shade2);
  popup->SetCallout(false);
}

void Redraw(IControl* control)
{
  if (control->GetUI()) control->GetUI()->SetAllControlsDirty();
  control->SetDirty(false);
}

class Panel final : public IControl
{
public:
  Panel(const IRECT& bounds, const char* title, EC2Next* plug)
  : IControl(bounds), mTitle(title), mPlug(plug) {}

  void Draw(IGraphics& g) override
  {
    g.FillRect(C(mPlug).primary, mRECT);
    g.FillRect(C(mPlug).shade1, IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 22.f));
    g.DrawLine(C(mPlug).shade2, mRECT.L, mRECT.T + 21.f, mRECT.R, mRECT.T + 21.f, nullptr, 1.f);
    g.DrawText(Text(mPlug, 15.f, EAlign::Near), mTitle.c_str(),
               IRECT(mRECT.L + 6.f, mRECT.T, mRECT.R - 4.f, mRECT.T + 22.f));
  }

private:
  std::string mTitle;
  EC2Next* mPlug;
};

class Background final : public IControl
{
public:
  Background(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug) {}
  void Draw(IGraphics& g) override { g.FillRect(C(mPlug).primary, mRECT); }
private:
  EC2Next* mPlug;
};

void ButtonFace(IGraphics& g, const EC2Next* plug, const IRECT& bounds, bool hovered,
                bool active = false)
{
  g.FillRect(active ? C(plug).red : C(plug).shade2, bounds);
  if (hovered && !active) g.FillRect(C(plug).hover, bounds);
  g.DrawLine(C(plug).shade3, bounds.L, bounds.B - 1.f, bounds.R, bounds.B - 1.f, nullptr, 1.f);
}

void DownArrow(IGraphics& g, const EC2Next* plug, const IRECT& bounds)
{
  const float x = bounds.R - 12.f;
  const float y = bounds.MH();
  g.FillTriangle(C(plug).muted, x - 3.f, y - 2.f, x + 3.f, y - 2.f, x, y + 2.f);
}

class FileMenu final : public IControl
{
public:
  FileMenu(const IRECT& bounds, EC2Next* plug, bool presets)
  : IControl(bounds), mPlug(plug), mPresets(presets)
  {
    if (mPresets)
    {
      mMenu.AddItem("Load Preset...");
      mMenu.AddItem("Save Preset...");
    }
    else
    {
      // The desktop app's sound file actions: replace, append, clear.
      mMenu.AddItem("Load Sample...");
      mMenu.AddItem("Add Sample...");
      mMenu.AddItem("Clear Samples");
    }
  }

  void Draw(IGraphics& g) override
  {
    ButtonFace(g, mPlug, mRECT, GetMouseIsOver());
    g.DrawText(Text(mPlug, 12.f, EAlign::Near), mPresets ? "Presets" : "Sound Files",
               IRECT(mRECT.L + 7.f, mRECT.T, mRECT.R - 16.f, mRECT.B));
    DownArrow(g, mPlug, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    StylePopup(mPlug);
    if (GetUI()) GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* selected, int) override
  {
    if (!selected || !mPlug || !GetUI()) return;
    const int choice = selected->GetChosenItemIdx();
    WDL_String fileName;
    WDL_String path;
    if (!mPresets && choice == 2)
    {
      mPlug->clearSamples();
      Redraw(this);
      return;
    }

    const bool save = mPresets && choice == 1;
    GetUI()->PromptForFile(fileName, path, save ? EFileAction::Save : EFileAction::Open,
                           mPresets ? "ec2p" : "wav");
    // On Windows the dialog returns the chosen directory in `path` and the full
    // file path in `fileName`. Using `path` opened a directory as a file, which
    // silently broke preset save/load and crashed sample loading.
    if (fileName.GetLength() == 0) return;
    if (mPresets)
    {
      if (save) mPlug->savePresetFile(fileName.Get());
      else mPlug->loadPresetFile(fileName.Get());
    }
    else if (choice == 1)
    {
      mPlug->addSample(fileName.Get());
    }
    else
    {
      mPlug->loadSample(fileName.Get());
    }
    Redraw(this);
  }

private:
  EC2Next* mPlug;
  bool mPresets;
  IPopupMenu mMenu;
};

class ViewMenu final : public IControl
{
public:
  ViewMenu(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    static const char* skins[] = {"Studio", "EC2 Dark", "EC2 Light"};
    static const char* sizes[] = {"50%", "75%", "100%", "125%", "150%", "200%"};
    for (const char* skin : skins) mMenu.AddItem(skin);
    mMenu.AddSeparator();
    for (const char* size : sizes) mMenu.AddItem(size);
    mMenu.AddSeparator();
    mMenu.AddItem("Set Current as Default");
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    ButtonFace(g, mPlug, mRECT, GetMouseIsOver());
    g.DrawText(Text(mPlug, 12.f, EAlign::Near), "View",
               IRECT(mRECT.L + 7.f, mRECT.T, mRECT.R - 16.f, mRECT.B));
    DownArrow(g, mPlug, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    static const int skinIds[] = {2, 0, 1};
    for (int i = 0; i < 3; ++i) mMenu.GetItem(i)->SetChecked(mPlug->editorSkin() == skinIds[i]);
    static const double scales[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    for (int i = 0; i < 6; ++i)
      mMenu.GetItem(i + 4)->SetChecked(std::abs(mPlug->editorScale() - scales[i]) < 0.001);
    StylePopup(mPlug);
    if (GetUI()) GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* selected, int) override
  {
    if (!selected) return;
    const int choice = selected->GetChosenItemIdx();
    static const double scales[] = {0.5, 0.75, 1.0, 1.25, 1.5, 2.0};
    static const int skinChoice[] = {2, 0, 1};
    if (choice >= 0 && choice < 3) mPlug->setEditorSkin(skinChoice[choice]);
    else if (choice >= 4 && choice < 10) mPlug->setEditorScale(scales[choice - 4]);
    else if (choice == 11) mPlug->saveEditorDefaults();
    Redraw(this);
  }

private:
  EC2Next* mPlug;
  IPopupMenu mMenu;
};

class FreeRun final : public IControl
{
public:
  FreeRun(const IRECT& bounds, EC2Next* plug) : IControl(bounds, kFreeRun), mPlug(plug) {}

  void Draw(IGraphics& g) override
  {
    const bool active = mPlug->GetParam(kFreeRun)->Bool();
    ButtonFace(g, mPlug, mRECT, GetMouseIsOver(), active);
    g.DrawText(Text(mPlug, 12.f, EAlign::Center, active ? kDark.text : C(mPlug).text), "FREE RUN", mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    SetValueFromUserInput(GetValue() > 0.5 ? 0.0 : 1.0);
    Redraw(this);
  }

private:
  EC2Next* mPlug;
};

class Menu final : public IControl
{
public:
  Menu(const IRECT& bounds, EC2Next* plug, int paramIdx, const std::vector<const char*>& labels,
       int shadeIndex)
  : IControl(bounds, paramIdx), mPlug(plug), mLabels(labels), mShadeIndex(shadeIndex)
  {
    for (const char* label : mLabels) mMenu.AddItem(label);
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(Shade(mPlug, mShadeIndex), mRECT);
    if (GetMouseIsOver()) g.FillRect(C(mPlug).hover, mRECT);
    int current = static_cast<int>(std::round(mPlug->GetParam(GetParamIdx())->Value()));
    current = std::max(0, std::min(static_cast<int>(mLabels.size()) - 1, current));
    g.DrawText(Text(mPlug, 11.f, EAlign::Near), mLabels[current],
               IRECT(mRECT.L + 6.f, mRECT.T, mRECT.R - 16.f, mRECT.B));
    DownArrow(g, mPlug, mRECT);
  }

  void OnMouseDown(float, float, const IMouseMod&) override
  {
    const int current = static_cast<int>(std::round(mPlug->GetParam(GetParamIdx())->Value()));
    for (int i = 0; i < mMenu.NItems(); ++i) mMenu.GetItem(i)->SetChecked(i == current);
    StylePopup(mPlug);
    if (GetUI()) GetUI()->CreatePopupMenu(*this, mMenu, mRECT);
  }

  void OnPopupMenuSelection(IPopupMenu* selected, int) override
  {
    if (!selected || !mPlug) return;
    const int choice = selected->GetChosenItemIdx();
    if (choice < 0 || choice >= static_cast<int>(mLabels.size())) return;
    SetValueFromUserInput(mPlug->GetParam(GetParamIdx())->ToNormalized(choice));
    Redraw(this);
  }

  void OnMouseWheel(float, float, const IMouseMod&, float delta) override
  {
    int next = static_cast<int>(std::round(mPlug->GetParam(GetParamIdx())->Value()));
    next += delta > 0.f ? 1 : -1;
    next = std::max(0, std::min(static_cast<int>(mLabels.size()) - 1, next));
    SetValueFromUserInput(mPlug->GetParam(GetParamIdx())->ToNormalized(next));
  }

private:
  EC2Next* mPlug;
  std::vector<const char*> mLabels;
  int mShadeIndex;
  IPopupMenu mMenu;
};

class RangeSlider final : public IControl
{
public:
  enum class Kind { Main, Modulation, LFO };

  RangeSlider(const IRECT& bounds, EC2Next* plug, int paramIdx, Kind kind, int group,
              bool integer = false, const char* unit = "", int shapeParamIdx = kNoParameter)
  : IControl(bounds, paramIdx)
  , mPlug(plug)
  , mKind(kind)
  , mGroup(group)
  , mInteger(integer)
  , mUnit(unit)
  , mShapeParamIdx(shapeParamIdx)
  {
    if (mKind == Kind::Main) SetTooltip(Ec2ParamName(group));
  }

  void Draw(IGraphics& g) override
  {
    Layout layout = GetLayout();
    const IColor& shade = Shade(mPlug, mGroup);
    g.FillRect(shade, mRECT);
    if (GetMouseIsOver()) g.FillRect(C(mPlug).hover, layout.track);

    const auto* param = mPlug->GetParam(GetParamIdx());
    double lo = 0.0, hi = 1.0;
    mPlug->parameterRange(GetParamIdx(), lo, hi);
    const double value = param->Value();
    const double norm = hi > lo ? Clamp((value - lo) / (hi - lo), 0.0, 1.0) : 0.0;

    char low[64];
    char high[64];
    char current[256];
    Format(low, sizeof(low), lo);
    Format(high, sizeof(high), hi);
    if (mKind == Kind::Main && mGroup == 7)
      snprintf(current, sizeof(current), "%s", mPlug->sampleName());
    else
    {
      Format(current, sizeof(current), value);
      if (mUnit && *mUnit)
      {
        const size_t len = std::strlen(current);
        snprintf(current + len, sizeof(current) - len, " %s", mUnit);
      }
    }

    // Grab first, text last: the grab sits at the current value, which is exactly
    // where the centered value text goes, so drawing it afterwards hid the digits.
    const float x = layout.track.L + static_cast<float>(norm) * layout.track.W();
    const IColor& grab = mKind == Kind::LFO ? C(mPlug).text : Accent(mPlug, mGroup);
    g.FillRect(grab, IRECT(x - 2.5f, layout.track.T + 2.f, x + 2.5f, layout.track.B - 2.f));

    g.DrawText(Text(mPlug, 11.f), low, layout.low);
    g.DrawText(Text(mPlug, 11.f), high, layout.high);
    g.DrawText(Text(mPlug, 11.f), current, layout.track);

    if (mKind == Kind::Main)
    {
      char name[96];
      snprintf(name, sizeof(name), "%d. %s", mGroup + 1, Ec2ParamName(mGroup));
      g.DrawText(Text(mPlug, 11.f, EAlign::Near), name, layout.name.GetPadded(-5.f, 0.f, 0.f, 0.f));
    }
    else if (mKind == Kind::LFO)
    {
      g.DrawText(Text(mPlug, 11.f), "Hz", layout.name);
    }
  }

  bool IsHit(float x, float y) const override
  {
    if (!IControl::IsHit(x, y)) return false;
    if (mKind != Kind::LFO || !Square()) return true;
    return y <= mRECT.T + mRECT.H() * 0.58f;
  }

  bool IsDirty() override
  {
    const bool square = Square();
    if (square != mLastSquare)
    {
      mLastSquare = square;
      return true;
    }
    return IControl::IsDirty();
  }

  void OnMouseDown(float x, float, const IMouseMod&) override
  {
    Layout layout = GetLayout();
    mLastX = x;
    if (layout.low.Contains(x, mRECT.MH()))
    {
      mEditing = 1;
      double lo = 0.0, hi = 1.0;
      mPlug->parameterRange(GetParamIdx(), lo, hi);
      mRangeStart = lo;
    }
    else if (layout.high.Contains(x, mRECT.MH()))
    {
      mEditing = 2;
      double lo = 0.0, hi = 1.0;
      mPlug->parameterRange(GetParamIdx(), lo, hi);
      mRangeStart = hi;
    }
    else
    {
      mEditing = 0;
      SetFromTrack(x);
    }
  }

  void OnMouseDrag(float x, float, float, float, const IMouseMod& mod) override
  {
    if (mEditing == 0)
    {
      SetFromTrack(x);
      return;
    }

    const auto* param = mPlug->GetParam(GetParamIdx());
    const double scale = (param->GetMax() - param->GetMin()) / 500.0 * (mod.S ? 0.1 : 1.0);
    EditRange(mEditing, mRangeStart + (x - mLastX) * scale);
  }

  void OnMouseWheel(float x, float y, const IMouseMod& mod, float delta) override
  {
    const auto* param = mPlug->GetParam(GetParamIdx());
    double lo = 0.0, hi = 1.0;
    mPlug->parameterRange(GetParamIdx(), lo, hi);
    const double step = mInteger ? 1.0 : std::max(param->GetStep(), (hi - lo) / 1000.0) * (mod.S ? 0.1 : 1.0);
    const double direction = delta > 0.f ? 1.0 : -1.0;
    Layout layout = GetLayout();
    if (layout.low.Contains(x, y)) EditRange(1, lo + direction * step);
    else if (layout.high.Contains(x, y)) EditRange(2, hi + direction * step);
    else SetRealValue(param->Value() + direction * step);
  }

  void OnMouseDblClick(float x, float y, const IMouseMod&) override
  {
    if (!GetUI()) return;
    Layout layout = GetLayout();
    int edit = 0;
    IRECT box = layout.track;
    double value = mPlug->GetParam(GetParamIdx())->Value();
    double lo = 0.0, hi = 1.0;
    mPlug->parameterRange(GetParamIdx(), lo, hi);
    if (layout.low.Contains(x, y)) { edit = 1; box = layout.low; value = lo; }
    else if (layout.high.Contains(x, y)) { edit = 2; box = layout.high; value = hi; }
    char text[64];
    Format(text, sizeof(text), value);
    GetUI()->CreateTextEntry(*this, Text(mPlug, 11.f), box, text, edit);
  }

  void OnTextEntryCompletion(const char* text, int valIdx) override
  {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text) return;
    if (valIdx == 0) SetRealValue(value);
    else EditRange(valIdx, value);
  }

private:
  struct Layout
  {
    IRECT low;
    IRECT track;
    IRECT high;
    IRECT name;
  };

  bool Square() const
  {
    return mShapeParamIdx != kNoParameter &&
           static_cast<int>(std::round(mPlug->GetParam(mShapeParamIdx)->Value())) == 1;
  }

  Layout GetLayout() const
  {
    IRECT row = mRECT;
    if (mKind == Kind::LFO && Square()) row.B = row.T + row.H() * 0.58f;
    const float lowW = mKind == Kind::Main ? 58.f : 52.f;
    const float nameW = mKind == Kind::Main ? 166.f : (mKind == Kind::LFO ? 24.f : 0.f);
    Layout result;
    result.low = IRECT(row.L, row.T, row.L + lowW, row.B);
    result.name = IRECT(row.R - nameW, row.T, row.R, row.B);
    result.high = IRECT(result.name.L - lowW, row.T, result.name.L, row.B);
    result.track = IRECT(result.low.R, row.T, result.high.L, row.B);
    return result;
  }

  void Format(char* out, size_t size, double value) const
  {
    if (mInteger) snprintf(out, size, "%d", static_cast<int>(std::round(value)));
    else snprintf(out, size, "%.3f", value);
  }

  void SetFromTrack(float x)
  {
    const IRECT track = GetLayout().track;
    if (track.W() <= 0.f) return;
    double lo = 0.0, hi = 1.0;
    mPlug->parameterRange(GetParamIdx(), lo, hi);
    const double norm = Clamp((x - track.L) / track.W(), 0.0, 1.0);
    SetRealValue(lo + (hi - lo) * norm);
  }

  void SetRealValue(double value)
  {
    const auto* param = mPlug->GetParam(GetParamIdx());
    double lo = 0.0, hi = 1.0;
    mPlug->parameterRange(GetParamIdx(), lo, hi);
    if (mInteger) value = std::round(value);
    value = Clamp(value, lo, hi);
    SetValueFromUserInput(param->ToNormalized(value));
  }

  void EditRange(int which, double value)
  {
    if (GetParamIdx() == kParamBase + 7) return;
    const auto* param = mPlug->GetParam(GetParamIdx());
    double lo = 0.0, hi = 1.0;
    mPlug->parameterRange(GetParamIdx(), lo, hi);
    const double minimum = mInteger ? 1.0 : std::max(1e-6, (param->GetMax() - param->GetMin()) * 1e-6);
    if (mInteger) value = std::round(value);
    if (which == 1) lo = Clamp(value, param->GetMin(), hi - minimum);
    else hi = Clamp(value, lo + minimum, param->GetMax());
    mPlug->setParameterRange(GetParamIdx(), lo, hi);
    mPlug->DirtyParametersFromUI();
    Redraw(this);
  }

  EC2Next* mPlug;
  Kind mKind;
  int mGroup;
  bool mInteger;
  const char* mUnit;
  int mShapeParamIdx;
  int mEditing = 0;
  float mLastX = 0.f;
  double mRangeStart = 0.0;
  bool mLastSquare = false;
};

class Duty final : public IControl
{
public:
  Duty(const IRECT& bounds, EC2Next* plug, int lfo, const IColor& shade)
  : IControl(bounds, kLfoBase + lfo * 4 + 3), mPlug(plug), mLfo(lfo), mShade(shade)
  {
  }

  void Draw(IGraphics& g) override
  {
    if (!Square()) return;
    mShade = Shade(mPlug, mLfo);
    g.FillRect(mShade, mRECT);
    const IRECT label(mRECT.L, mRECT.T, mRECT.L + 42.f, mRECT.B);
    const IRECT track(label.R, mRECT.T, mRECT.R - 24.f, mRECT.B);
    g.DrawText(Text(mPlug, 10.f), "Duty", label);
    const double value = mPlug->GetParam(GetParamIdx())->Value();
    char text[32];
    snprintf(text, sizeof(text), "%.3f", value);
    g.DrawText(Text(mPlug, 10.f), text, track);
    const float x = track.L + static_cast<float>(value) * track.W();
    g.FillRect(C(mPlug).text, IRECT(x - 2.f, track.T + 1.f, x + 2.f, track.B - 1.f));
  }

  bool IsHit(float x, float y) const override { return Square() && IControl::IsHit(x, y); }

  bool IsDirty() override
  {
    const bool square = Square();
    if (square != mLastSquare)
    {
      mLastSquare = square;
      return true;
    }
    return IControl::IsDirty();
  }

  void OnMouseDown(float x, float, const IMouseMod&) override { Set(x); }
  void OnMouseDrag(float x, float, float, float, const IMouseMod&) override { Set(x); }

private:
  bool Square() const
  {
    return static_cast<int>(std::round(mPlug->GetParam(kLfoBase + mLfo * 4)->Value())) == 1;
  }

  void Set(float x)
  {
    const float left = mRECT.L + 42.f;
    const float right = mRECT.R - 24.f;
    if (right <= left) return;
    SetValueFromUserInput(Clamp((x - left) / (right - left), 0.0, 1.0));
  }

  EC2Next* mPlug;
  int mLfo;
  IColor mShade;
  bool mLastSquare = false;
};

class Morph final : public IControl
{
public:
  Morph(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    SetDisabled(true);
    SetMouseOverWhenDisabled(true);
    SetTooltip("Preset morphing is not implemented");
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(C(mPlug).shade1, mRECT);
    g.DrawText(Text(mPlug, 11.f, EAlign::Near, C(mPlug).muted), "0.000", mRECT.GetPadded(-4.f, 0.f, 0.f, 0.f));
    g.DrawText(Text(mPlug, 11.f, EAlign::Far, C(mPlug).muted), "Morph Time", mRECT.GetPadded(0.f, 0.f, -4.f, 0.f));
  }

private:
  EC2Next* mPlug;
};

class Presets final : public IControl
{
public:
  Presets(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    const Layout layout = GetLayout();
    char current[64];
    if (mPlug->presetIndex() >= 0) snprintf(current, sizeof(current), "Current Preset: %d", mPlug->presetIndex());
    else snprintf(current, sizeof(current), "Current Preset: none");
    g.DrawText(Text(mPlug, 11.f, EAlign::Near), current, layout.current);

    for (int i = 0; i < 36; ++i)
    {
      const IRECT cell = Cell(layout.grid, i);
      const bool available = i < mPlug->presetCount();
      g.FillRect(i == mPlug->presetIndex() ? C(mPlug).blue : (available ? C(mPlug).shade2 : C(mPlug).shade1), cell);
      char number[8];
      snprintf(number, sizeof(number), "%d", i);
      const IColor& text = i == mPlug->presetIndex() ? kDark.text : (available ? C(mPlug).text : C(mPlug).shade3);
      g.DrawText(Text(mPlug, 9.f, EAlign::Center, text), number, cell);
    }

    g.FillRect(C(mPlug).shade2, layout.previous);
    g.FillRect(C(mPlug).shade2, layout.next);
    g.FillRect(C(mPlug).shade2, layout.store);
    g.DrawText(Text(mPlug, 11.f), "<-", layout.previous);
    g.DrawText(Text(mPlug, 11.f), "->", layout.next);
    g.DrawText(Text(mPlug, 11.f), "Store", layout.store);
    g.DrawText(Text(mPlug, 11.f, EAlign::Near, C(mPlug).muted), "default", layout.map);
    g.DrawText(Text(mPlug, 11.f, EAlign::Center, C(mPlug).muted), "Preset Map", layout.map);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override
  {
    const Layout layout = GetLayout();
    if (layout.grid.Contains(x, y))
    {
      for (int i = 0; i < 36; ++i)
      {
        if (Cell(layout.grid, i).Contains(x, y) && i < mPlug->presetCount())
        {
          mPlug->loadPreset(i);
          break;
        }
      }
    }
    else if (layout.previous.Contains(x, y)) mPlug->previousPreset();
    else if (layout.next.Contains(x, y)) mPlug->nextPreset();
    else if (layout.store.Contains(x, y)) mPlug->saveNewPreset();
    Redraw(this);
  }

private:
  struct Layout
  {
    IRECT current;
    IRECT grid;
    IRECT previous;
    IRECT next;
    IRECT store;
    IRECT map;
  };

  Layout GetLayout() const
  {
    Layout result;
    result.current = IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 16.f);
    result.grid = IRECT(mRECT.L, result.current.B + 1.f, mRECT.R, result.current.B + 37.f);
    const float buttons = result.grid.B + 2.f;
    result.previous = IRECT(mRECT.L, buttons, mRECT.L + 30.f, buttons + 20.f);
    result.next = IRECT(result.previous.R + 2.f, buttons, result.previous.R + 32.f, buttons + 20.f);
    result.store = IRECT(result.next.R + 4.f, buttons, result.next.R + 58.f, buttons + 20.f);
    result.map = IRECT(mRECT.L, buttons + 22.f, mRECT.R, mRECT.B);
    return result;
  }

  static IRECT Cell(const IRECT& grid, int index)
  {
    const float w = grid.W() / 12.f;
    const float h = grid.H() / 3.f;
    const int column = index % 12;
    const int row = index / 12;
    return IRECT(grid.L + column * w + 1.f, grid.T + row * h + 1.f,
                 grid.L + (column + 1) * w - 1.f, grid.T + (row + 1) * h - 1.f);
  }

  EC2Next* mPlug;
};

class ScanDisplay final : public IControl
{
public:
  ScanDisplay(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    g.FillRect(C(mPlug).shade1, mRECT);
    int frames = 0, channels = 0;
    const float* data = mPlug->waveform(frames, channels);
    if (!data || frames <= 1 || channels <= 0)
    {
      g.DrawText(Text(mPlug, 12.f), "No Sound Files", mRECT);
      return;
    }

    const float middle = mRECT.MH();
    const float scale = mRECT.H() * 0.48f;
    const int columns = std::max(1, static_cast<int>(mRECT.W()));
    for (int column = 0; column < columns; ++column)
    {
      const int begin = static_cast<int>(static_cast<long long>(column) * frames / columns);
      int end = static_cast<int>(static_cast<long long>(column + 1) * frames / columns);
      end = std::max(begin + 1, end);
      float lo = 0.f, hi = 0.f;
      for (int i = begin; i < end && i < frames; ++i)
      {
        const float sample = data[static_cast<size_t>(i) * channels];
        lo = std::min(lo, sample);
        hi = std::max(hi, sample);
      }
      const float x = mRECT.L + static_cast<float>(column);
      g.DrawLine(C(mPlug).text, x, middle - hi * scale, x, middle - lo * scale, nullptr, 1.f);
    }

    const float begin = static_cast<float>(mPlug->GetParam(kParamBase + 8)->Value());
    const float range = static_cast<float>(mPlug->GetParam(kParamBase + 9)->Value());
    const IColor band(100, C(mPlug).blue.R, C(mPlug).blue.G, C(mPlug).blue.B);
    auto span = [&](float a, float b) {
      a = std::max(0.f, std::min(1.f, a));
      b = std::max(0.f, std::min(1.f, b));
      if (b > a) g.FillRect(band, IRECT(mRECT.L + a * mRECT.W(), mRECT.T,
                                       mRECT.L + b * mRECT.W(), mRECT.B));
    };
    const float end = begin + range;
    if (std::abs(range) >= 1.f) span(0.f, 1.f);
    else if (range >= 0.f)
    {
      if (end <= 1.f) span(begin, end);
      else { span(begin, 1.f); span(0.f, std::fmod(end, 1.f)); }
    }
    else
    {
      if (end >= 0.f) span(end, begin);
      else { span(0.f, begin); span(1.f + std::fmod(end, 1.f), 1.f); }
    }

    const float headX = mRECT.L + begin * mRECT.W();
    g.DrawLine(C(mPlug).blue, headX, mRECT.T, headX, mRECT.B, nullptr, 4.f);
    const float scan = mPlug->scanPosition();
    const float scanX = mRECT.L + scan * mRECT.W();
    g.DrawLine(C(mPlug).text, scanX, mRECT.T, scanX, mRECT.B, nullptr, 1.f);

    float positions[128];
    const int count = mPlug->grains(positions, 128);
    for (int i = 0; i < count; ++i)
    {
      const float position = std::max(0.f, std::min(1.f, positions[i] / frames));
      const float x = mRECT.L + position * mRECT.W();
      g.DrawLine(C(mPlug).red, x, mRECT.T, x, mRECT.B, nullptr, 1.f);
    }
  }

private:
  EC2Next* mPlug;
};

class GrainHistory final : public IControl
{
public:
  GrainHistory(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    mHistory.fill(0);
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    float left = 0.f, right = 0.f;
    int grains = 0;
    mPlug->meters(left, right, grains);
    mAccum = std::max(mAccum, grains);
    if ((++mFrame & 1) == 0)
    {
      std::move(mHistory.begin() + 1, mHistory.end(), mHistory.begin());
      mHistory.back() = mAccum;
      mAccum = 0;
    }

    char counter[48];
    snprintf(counter, sizeof(counter), "Counter: %d", grains);
    g.DrawText(Text(mPlug, 11.f, EAlign::Near), counter, IRECT(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 17.f));
    const IRECT plot(mRECT.L, mRECT.T + 19.f, mRECT.R, mRECT.B);
    g.FillRect(C(mPlug).shade1, plot);
    const int maximum = std::max(2, *std::max_element(mHistory.begin(), mHistory.end()));
    const float width = plot.W() / static_cast<float>(mHistory.size());
    const IColor& histogram = C(mPlug).histogram;
    for (size_t i = 0; i < mHistory.size(); ++i)
    {
      const float height = plot.H() * mHistory[i] / maximum;
      g.FillRect(histogram, IRECT(plot.L + i * width, plot.B - height,
                                 plot.L + (i + 1) * width, plot.B));
    }
  }

private:
  EC2Next* mPlug;
  std::array<int, 80> mHistory;
  int mAccum = 0;
  int mFrame = 0;
};

class Oscilloscope final : public IControl
{
public:
  Oscilloscope(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    const IRECT slider(mRECT.L, mRECT.T, mRECT.R, mRECT.T + 20.f);
    g.FillRect(C(mPlug).shade2, slider);
    g.DrawText(Text(mPlug, 11.f, EAlign::Near), "Time Frame (s):", IRECT(slider.L + 3.f, slider.T, slider.L + 105.f, slider.B));
    char time[32];
    snprintf(time, sizeof(time), "%.3f", mTimeFrame);
    g.DrawText(Text(mPlug, 11.f), time, slider);
    const float grab = slider.L + static_cast<float>((mTimeFrame - 0.001) / 2.999) * slider.W();
    g.FillRect(C(mPlug).shade3, IRECT(grab - 3.f, slider.T + 2.f, grab + 3.f, slider.B - 2.f));

    const IRECT plot(mRECT.L, slider.B + 2.f, mRECT.R, mRECT.B);
    g.FillRect(C(mPlug).shade1, plot);
    g.DrawLine(C(mPlug).shade2, plot.L, plot.MH(), plot.R, plot.MH(), nullptr, 1.f);

    const int frames = std::max(1, std::min(300000, static_cast<int>(mTimeFrame * mPlug->sampleRate())));
    if (static_cast<int>(mLeft.size()) < frames)
    {
      mLeft.resize(frames);
      mRight.resize(frames);
    }
    const int count = mPlug->readScope(mLeft.data(), mRight.data(), frames);
    if (count <= 0) return;
    DrawChannel(g, plot, mLeft.data(), count, C(mPlug).blue);
    DrawChannel(g, plot, mRight.data(), count, C(mPlug).red);
  }

  void OnMouseDown(float x, float y, const IMouseMod&) override { SetTime(x, y); }
  void OnMouseDrag(float x, float y, float, float, const IMouseMod&) override { SetTime(x, y); }

private:
  void SetTime(float x, float y)
  {
    if (y > mRECT.T + 20.f || mRECT.W() <= 0.f) return;
    mTimeFrame = 0.001 + Clamp((x - mRECT.L) / mRECT.W(), 0.0, 1.0) * 2.999;
  }

  static void DrawChannel(IGraphics& g, const IRECT& plot, const float* data, int frames,
                          const IColor& color)
  {
    const int columns = std::max(1, static_cast<int>(plot.W()));
    float previousX = plot.L;
    float previousY = plot.MH();
    for (int column = 0; column < columns; ++column)
    {
      const int begin = static_cast<int>(static_cast<long long>(column) * frames / columns);
      int end = static_cast<int>(static_cast<long long>(column + 1) * frames / columns);
      end = std::max(begin + 1, std::min(frames, end));
      float lo = 0.f, hi = 0.f;
      for (int i = begin; i < end; ++i)
      {
        lo = std::min(lo, data[i]);
        hi = std::max(hi, data[i]);
      }
      const float x = plot.L + static_cast<float>(column);
      const float top = plot.MH() - hi * plot.H() * 0.48f;
      const float bottom = plot.MH() - lo * plot.H() * 0.48f;
      g.DrawLine(color, x, top, x, bottom, nullptr, 1.f);
      const float mid = (top + bottom) * 0.5f;
      if (column > 0) g.DrawLine(color, previousX, previousY, x, mid, nullptr, 1.f);
      previousX = x;
      previousY = mid;
    }
  }

  EC2Next* mPlug;
  double mTimeFrame = 1.0;
  std::vector<float> mLeft;
  std::vector<float> mRight;
};

class VU final : public IControl
{
public:
  VU(const IRECT& bounds, EC2Next* plug) : IControl(bounds), mPlug(plug)
  {
    SetAnimation([](IControl*) {});
  }

  void Draw(IGraphics& g) override
  {
    float peaks[2] = {0.f, 0.f};
    int grains = 0;
    mPlug->meters(peaks[0], peaks[1], grains);
    // Peak hold with decay: the meter is fed from a block-rate snapshot, so a
    // single empty block would otherwise flash the panel black.
    for (int channel = 0; channel < 2; ++channel)
    {
      mHold[channel] = std::max(peaks[channel], mHold[channel] * 0.92f);
      peaks[channel] = mHold[channel];
    }
    const float gap = 5.f;
    const float width = (mRECT.W() - gap) * 0.5f;
    for (int channel = 0; channel < 2; ++channel)
    {
      const float left = mRECT.L + channel * (width + gap);
      const IRECT track(left, mRECT.T, left + width, mRECT.B);
      g.FillRect(C(mPlug).black, track);
      const float peak = peaks[channel];
      const float db = 20.f * std::log10(std::max(peak, 0.000001f));
      const float norm = std::max(0.f, std::min(1.f, (db + 60.f) / 60.f));
      g.FillRect(peak >= 1.f ? C(mPlug).red : C(mPlug).green,
                 IRECT(track.L, track.B - track.H() * norm, track.R, track.B));
    }
  }

private:
  EC2Next* mPlug;
  float mHold[2] = {0.f, 0.f};
};

IRECT AttachPanel(IGraphics* g, const IRECT& bounds, const char* title, EC2Next* plug)
{
  g->AttachControl(new Panel(bounds, title, plug));
  return IRECT(bounds.L + 4.f, bounds.T + 22.f, bounds.R - 4.f, bounds.B - 4.f);
}

} // namespace

// EC2-style controls are hand-drawn because framework styled widgets previously
// rendered invisibly on the dark panel.
void BuildEC2Editor(IGraphics* graphics, EC2Next* plug)
{
  // Layout runs again on every resize. Clearing first means a relayout cannot
  // accumulate controls: whatever the framework keeps, this drops the old set.
  graphics->RemoveControls(0);
  graphics->LoadFont("Roboto", ROBOTO_FN);
  // window freely in both axes and relayouts through the framework, so nothing
  // Resizing must re-run this layout through the framework. Doing it by hand
  // inside the resize callback deleted controls while they were being drawn.
  // The bottom-right handle is the only resize affordance: Size mode resizes the
  graphics->EnableMouseOver(true);
  // calls Resize from a host notification (which leaked per call).
  graphics->AttachPopupMenuControl(Text(plug, 12.f, EAlign::Near));
  StylePopup(plug);
  graphics->SetScaleConstraints(0.5f, 2.0f);
  graphics->AttachCornerResizer(EUIResizerMode::Size, true, COLOR_TRANSLUCENT,
                                C(plug).red, C(plug).blue, 18.f);
  graphics->SetDisplayTickFunc([graphics, plug]() {
    plug->syncEditorScale(graphics->GetDrawScale());
  });
  graphics->AttachControl(new Background(graphics->GetBounds(), plug));

  const float width = static_cast<float>(graphics->Width());
  const float height = static_cast<float>(graphics->Height());

  const float strip = 22.f;
  graphics->AttachControl(new FileMenu(IRECT(0.f, 0.f, 100.f, strip), plug, false));
  graphics->AttachControl(new FileMenu(IRECT(100.f, 0.f, 182.f, strip), plug, true));
  graphics->AttachControl(new ViewMenu(IRECT(182.f, 0.f, 244.f, strip), plug));
  graphics->AttachControl(new FreeRun(IRECT(width - 90.f, 0.f, width, strip), plug));

  const float available = height - strip;
  const float topHeight = available * 0.385f;
  const float secondHeight = available * 0.18f;
  const float secondY = strip + topHeight;
  const float graphY = secondY + secondHeight;
  const float graphHeight = height - graphY;

  const float half = width * 0.5f;

  const IRECT granulation = AttachPanel(graphics, IRECT(0.f, strip, half, secondY), "    GRANULATION CONTROLS", plug);
  const IRECT modulation = AttachPanel(graphics, IRECT(half, strip, width, secondY), "    MODULATION CONTROLS", plug);
  const float mainRow = granulation.H() / kEcParamCount;
  const float modRow = modulation.H() / kEcParamCount;
  static const std::vector<const char*> lfoNames{"LFO1", "LFO2", "LFO3", "LFO4", "LFO5", "LFO6"};

  for (int i = 0; i < kEcParamCount; ++i)
  {
    const bool integer = i == 3 || i == 7;
    const char* unit = i == 0 || i == 5 ? "Hz" : (i == 11 ? "ms" : (i == 14 ? "dB" : ""));
    graphics->AttachControl(new RangeSlider(
      IRECT(granulation.L, granulation.T + i * mainRow, granulation.R, granulation.T + (i + 1) * mainRow),
      plug, kParamBase + i, RangeSlider::Kind::Main, i, integer, unit));

    const IRECT row(modulation.L, modulation.T + i * modRow,
                    modulation.R, modulation.T + (i + 1) * modRow);
    graphics->AttachControl(new Menu(IRECT(row.L, row.T, row.L + 66.f, row.B), plug,
                                     kModLfoBase + i, lfoNames, i));
    graphics->AttachControl(new RangeSlider(IRECT(row.L + 66.f, row.T, row.R, row.B), plug,
                                            kModBase + i, RangeSlider::Kind::Modulation, i));
  }

  const IRECT presets = AttachPanel(graphics, IRECT(0.f, secondY, half, graphY), "    PRESETS", plug);
  const IRECT presetGrid(presets.L, presets.T, presets.R, presets.B - 20.f);
  graphics->AttachControl(new Presets(presetGrid, plug));
  graphics->AttachControl(new Morph(IRECT(presets.L, presets.B - 20.f, presets.R, presets.B), plug));

  const IRECT lfos = AttachPanel(graphics, IRECT(half, secondY, width, graphY), "    LFO CONTROLS", plug);
  const float lfoRow = lfos.H() / kLfoCount;
  static const std::vector<const char*> shapes{"Sine", "Square", "Rise", "Fall", "Noise"};
  static const std::vector<const char*> polarities{"BI", "UNI+", "UNI-"};
  for (int i = 0; i < kLfoCount; ++i)
  {
    const IRECT row(lfos.L, lfos.T + i * lfoRow, lfos.R, lfos.T + (i + 1) * lfoRow);
    const int base = kLfoBase + i * 4;
    graphics->AttachControl(new ITextControl(IRECT(row.L, row.T, row.L + 40.f, row.B),
                                            (std::string("LFO") + std::to_string(i + 1)).c_str(), Text(plug, 11.f)));
    graphics->AttachControl(new Menu(IRECT(row.L + 40.f, row.T, row.L + 110.f, row.B), plug,
                                     base, shapes, i));
    graphics->AttachControl(new Menu(IRECT(row.L + 110.f, row.T, row.L + 164.f, row.B), plug,
                                     base + 1, polarities, i));
    graphics->AttachControl(new RangeSlider(IRECT(row.L + 164.f, row.T, row.R, row.B), plug,
                                            base + 2, RangeSlider::Kind::LFO, i, false, "", base));
    graphics->AttachControl(new Duty(IRECT(row.L + 164.f, row.T + row.H() * 0.58f, row.R, row.B),
                                     plug, i, Shade(plug, i)));
  }

  const float scanHeight = graphHeight / 3.f;
  const float lowerY = graphY + scanHeight;
  const IRECT scan = AttachPanel(graphics, IRECT(0.f, graphY, width, lowerY), "    SCAN DISPLAY", plug);
  graphics->AttachControl(new ScanDisplay(scan, plug));

  const float grainsRight = width * 0.25f;
  const float vuLeft = width * 15.f / 16.f;
  const IRECT grains = AttachPanel(graphics, IRECT(0.f, lowerY, grainsRight, height), "    ACTIVE GRAINS", plug);
  graphics->AttachControl(new GrainHistory(grains, plug));
  const IRECT scope = AttachPanel(graphics, IRECT(grainsRight, lowerY, vuLeft, height), "    OSCILLOSCOPE", plug);
  graphics->AttachControl(new Oscilloscope(scope, plug));
  const IRECT vu = AttachPanel(graphics, IRECT(vuLeft, lowerY, width, height), "    VU", plug);
  graphics->AttachControl(new VU(vu, plug));
}
