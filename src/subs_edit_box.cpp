// Copyright (c) 2005, Rodrigo Braz Monteiro
// Copyright (c) 2010, Thomas Goyne <plorkyeran@aegisub.org>
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file subs_edit_box.cpp
/// @brief Main subtitle editing area, including toolbars around the text control
/// @ingroup main_ui

#include "subs_edit_box.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "base_grid.h"
#include "command/command.h"
#include "compat.h"
#include "dialog_style_editor.h"
#include "flyweight_hash.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "initial_line_state.h"
#include "options.h"
#include "placeholder_ctrl.h"
#include "project.h"
#include "selection_controller.h"
#include "subs_edit_ctrl.h"
#include "text_selection_controller.h"
#include "timeedit_ctrl.h"
#include "utils.h"
#include "validators.h"
#include "video_controller.h"
#include "video_frame.h"

#include <libaegisub/ass/uuencode.h>
#include <libaegisub/cajun/writer.h>
#include <libaegisub/character_count.h>
#include <libaegisub/fs.h>
#include <libaegisub/io.h>
#include <libaegisub/json.h>
#include <libaegisub/path.h>
#include <libaegisub/util.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iterator>
#include <unordered_set>

#include <wx/bmpbndl.h>
#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/clipbrd.h>
#include <wx/dialog.h>
#include <wx/dcmemory.h>
#include <wx/fontenum.h>
#include <wx/image.h>
#include <wx/listbox.h>
#include <wx/msgdlg.h>
#include <wx/radiobut.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/wrapsizer.h>

wxBitmap make_blank_actor_button_bitmap(int size) {
	wxImage image(size, size);
	image.SetRGB(wxRect(0, 0, size, size), 255, 255, 255);
	return wxBitmap(image);
}

wxBitmap make_symbol_actor_button_bitmap(int size, wxString const& symbol) {
	wxBitmap bitmap = make_blank_actor_button_bitmap(size);
	wxMemoryDC dc(bitmap);
	dc.SetBackground(*wxWHITE_BRUSH);
	dc.Clear();

	wxFont font = dc.GetFont();
	font.SetPointSize(std::max(12, size / 4));
	font.SetWeight(wxFONTWEIGHT_BOLD);
	dc.SetFont(font);
	dc.SetTextForeground(wxColour(48, 48, 48));

	wxSize text_size = dc.GetTextExtent(symbol);
	dc.DrawText(symbol, std::max(0, (size - text_size.x) / 2), std::max(0, (size - text_size.y) / 2));
	dc.SelectObject(wxNullBitmap);
	return bitmap;
}

class ActorPresetButton final : public wxPanel {
	wxStaticBitmap *image = nullptr;
	wxStaticText *label = nullptr;
	int image_size = 0;

	void EmitClick() {
		wxCommandEvent evt(wxEVT_BUTTON, GetId());
		evt.SetEventObject(this);
		GetEventHandler()->ProcessEvent(evt);
	}

	void BindClickForwarder(wxWindow *window) {
		window->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) { EmitClick(); });
	}

public:
	ActorPresetButton(wxWindow *parent, wxWindowID id, int size)
	: wxPanel(parent, id, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxBORDER_THEME)
	, image_size(size)
	{
		SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE));

		auto *sizer = new wxBoxSizer(wxVERTICAL);
		image = new wxStaticBitmap(this, -1, wxBitmap(size, size));
		image->SetMinSize(wxSize(size, size));
		label = new wxStaticText(this, -1, "", wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
		label->SetMinSize(wxSize(size + FromDIP(20), -1));

		sizer->Add(image, wxSizerFlags().CenterHorizontal().Border(wxLEFT | wxRIGHT | wxTOP, 6));
		sizer->Add(label, wxSizerFlags().Expand().Border(wxALL, 6));
		SetSizer(sizer);
		SetMinSize(wxSize(size + FromDIP(24), size + FromDIP(54)));

		BindClickForwarder(this);
		BindClickForwarder(image);
		BindClickForwarder(label);
	}

	void SetActorLabel(wxString const& text) {
		label->SetLabelText(text);
		label->Wrap(std::max(1, image_size + FromDIP(20)));
		Layout();
	}

	void SetActorBitmap(wxBitmap const& bitmap) {
		image->SetBitmap(bitmap.IsOk() ? bitmap : make_blank_actor_button_bitmap(image_size));
	}

	void SetActorToolTip(wxString const& text) {
		wxPanel::SetToolTip(text);
		image->SetToolTip(text);
		label->SetToolTip(text);
	}
};

namespace {
constexpr size_t kLegacyActorPresetCount = 5;
constexpr int kActorButtonMinSize = 64;
constexpr int kActorButtonMaxSize = 256;
constexpr int64_t kActorButtonPresetVersion = 1;

struct ActorButtonDefinition {
	std::string label;
	std::string value;
	std::string image;
};

std::string actor_button_option(size_t index, char const* field) {
	return "Subtitle/Actor Buttons/Button " + std::to_string(index + 1) + "/" + field;
}

std::string actor_button_value(ActorButtonDefinition const& def) {
	if (!def.label.empty()) return def.label;
	return def.value;
}

void normalize_actor_button_definition(ActorButtonDefinition &def) {
	if (def.label.empty())
		def.label = def.value;
	def.value = def.label;
}

std::vector<ActorButtonDefinition> normalize_actor_button_definitions(std::vector<ActorButtonDefinition> definitions) {
	for (auto &def : definitions)
		normalize_actor_button_definition(def);
	return definitions;
}

std::string escape_actor_button_field(std::string const& field) {
	std::string out;
	out.reserve(field.size());
	for (char ch : field) {
		switch (ch) {
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n"; break;
			case '\r': out += "\\r"; break;
			case '\t': out += "\\t"; break;
			default: out += ch; break;
		}
	}
	return out;
}

std::string unescape_actor_button_field(std::string const& field) {
	std::string out;
	out.reserve(field.size());
	bool escaping = false;
	for (char ch : field) {
		if (!escaping) {
			if (ch == '\\') {
				escaping = true;
				continue;
			}
			out += ch;
			continue;
		}

		switch (ch) {
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			default: out += ch; break;
		}
		escaping = false;
	}
	if (escaping) out += '\\';
	return out;
}

std::vector<std::string> split_string(std::string const& text, char separator) {
	std::vector<std::string> parts;
	size_t start = 0;
	while (true) {
		size_t end = text.find(separator, start);
		parts.push_back(text.substr(start, end == std::string::npos ? std::string::npos : end - start));
		if (end == std::string::npos) break;
		start = end + 1;
	}
	return parts;
}

std::vector<ActorButtonDefinition> parse_actor_button_definitions(std::string const& serialized) {
	std::vector<ActorButtonDefinition> definitions;
	if (serialized.empty()) return definitions;

	for (auto const& record : split_string(serialized, '\n')) {
		if (record.empty()) continue;

		auto fields = split_string(record, '\t');
		ActorButtonDefinition def;
		if (!fields.empty()) def.label = unescape_actor_button_field(fields[0]);
		if (fields.size() > 1) def.value = unescape_actor_button_field(fields[1]);
		if (fields.size() > 2) def.image = unescape_actor_button_field(fields[2]);

		if (!def.label.empty() || !def.value.empty() || !def.image.empty())
			definitions.push_back(std::move(def));
	}

	return normalize_actor_button_definitions(std::move(definitions));
}

std::string serialize_actor_button_definitions(std::vector<ActorButtonDefinition> const& definitions) {
	std::string serialized;
	for (auto const& def : definitions) {
		auto normalized = def;
		normalize_actor_button_definition(normalized);
		if (normalized.label.empty() && normalized.value.empty() && normalized.image.empty()) continue;
		if (!serialized.empty()) serialized += '\n';
		serialized += escape_actor_button_field(normalized.label);
		serialized += '\t';
		serialized += escape_actor_button_field(normalized.value);
		serialized += '\t';
		serialized += escape_actor_button_field(normalized.image);
	}
	return serialized;
}

bool legacy_actor_buttons_customized() {
	for (size_t i = 0; i < kLegacyActorPresetCount; ++i) {
		if (!OPT_GET(actor_button_option(i, "Label"))->IsDefault()) return true;
		if (!OPT_GET(actor_button_option(i, "Value"))->IsDefault()) return true;
		if (!OPT_GET(actor_button_option(i, "Image"))->IsDefault()) return true;
	}
	return false;
}

std::vector<ActorButtonDefinition> legacy_actor_button_definitions() {
	std::vector<ActorButtonDefinition> definitions;
	definitions.reserve(kLegacyActorPresetCount);
	for (size_t i = 0; i < kLegacyActorPresetCount; ++i) {
		ActorButtonDefinition def;
		def.label = OPT_GET(actor_button_option(i, "Label"))->GetString();
		def.value = OPT_GET(actor_button_option(i, "Value"))->GetString();
		def.image = OPT_GET(actor_button_option(i, "Image"))->GetString();
		if (!def.label.empty() || !def.value.empty() || !def.image.empty())
			definitions.push_back(std::move(def));
	}
	return normalize_actor_button_definitions(std::move(definitions));
}

std::vector<ActorButtonDefinition> get_actor_button_definitions() {
	auto definitions = parse_actor_button_definitions(OPT_GET("Subtitle/Actor Buttons/Definitions")->GetString());
	if (!definitions.empty()) {
		auto definitions_opt = OPT_GET("Subtitle/Actor Buttons/Definitions");
		if (!definitions_opt->IsDefault() || !legacy_actor_buttons_customized())
			return definitions;
	}

	auto legacy_definitions = legacy_actor_button_definitions();
	if (!legacy_definitions.empty()) return legacy_definitions;
	return definitions;
}

wxString actor_button_display_name(ActorButtonDefinition const& def, size_t index) {
	if (!def.label.empty()) return to_wx(def.label);
	if (!def.value.empty()) return to_wx(def.value);
	return wxString::Format(_("Actor %d"), int(index + 1));
}

wxImage crop_to_square(wxImage image) {
	if (!image.IsOk()) return image;

	int side = std::min(image.GetWidth(), image.GetHeight());
	if (side <= 0) return wxImage();

	int x = (image.GetWidth() - side) / 2;
	int y = (image.GetHeight() - side) / 2;
	return image.GetSubImage(wxRect(x, y, side, side));
}

wxBitmap load_actor_button_bitmap(std::string const& encoded_path, int bitmap_size) {
	if (encoded_path.empty()) return wxBitmap();

	auto image_path = config::path->Decode(encoded_path);
	if (!agi::fs::FileExists(image_path)) return wxBitmap();

	wxImage image(wxString(image_path.wstring()), wxBITMAP_TYPE_ANY);
	if (!image.IsOk()) return wxBitmap();

	image = crop_to_square(std::move(image));
	image.Rescale(bitmap_size, bitmap_size, wxIMAGE_QUALITY_HIGH);
	return wxBitmap(image);
}

bool read_bitmap_from_clipboard(wxBitmap &bitmap) {
	if (!wxTheClipboard->Open()) return false;

	wxBitmapDataObject data;
	bool ok = wxTheClipboard->IsSupported(wxDF_BITMAP) && wxTheClipboard->GetData(data);
	wxTheClipboard->Close();
	if (!ok) return false;

	bitmap = data.GetBitmap();
	return bitmap.IsOk();
}

agi::fs::path make_actor_button_image_path(std::string extension = ".png") {
	static uint64_t counter = 0;
	auto directory = config::path->Decode("?user/actor-buttons");
	agi::fs::CreateDirectory(directory);

	if (extension.empty())
		extension = ".png";
	if (extension[0] != '.')
		extension.insert(extension.begin(), '.');

	auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	return directory / agi::format("button-%lld-%llu%s",
		static_cast<long long>(timestamp),
		static_cast<unsigned long long>(++counter),
		extension);
}

bool save_actor_button_image(wxImage const& image, std::string &encoded_path, bool crop_square = false) {
	if (!image.IsOk()) return false;

	wxImage output = crop_square ? crop_to_square(image.Copy()) : image.Copy();
	if (!output.IsOk()) return false;

	auto path = make_actor_button_image_path(".png");

	if (!output.SaveFile(wxString(path.wstring()), wxBITMAP_TYPE_PNG))
		return false;

	encoded_path = config::path->Encode(path);
	return true;
}

bool read_actor_button_image_bytes(std::string const& encoded_path, std::string &data, std::string &extension) {
	if (encoded_path.empty()) return false;

	auto path = config::path->Decode(encoded_path);
	if (!agi::fs::FileExists(path)) return false;

	auto stream = agi::io::Open(path, true);
	data.assign(std::istreambuf_iterator<char>(*stream), std::istreambuf_iterator<char>());
	extension = path.extension().string();
	return true;
}

bool save_actor_button_image_bytes(std::string const& data, std::string const& extension, std::string &encoded_path) {
	auto path = make_actor_button_image_path(extension);

	agi::io::Save save(path, true);
	save.Get().write(data.data(), static_cast<std::streamsize>(data.size()));
	if (!save.Get())
		return false;

	encoded_path = config::path->Encode(path);
	return true;
}

bool read_json_string(json::Object const& object, char const* key, std::string &value) {
	auto it = object.find(key);
	if (it == object.end()) return false;
	value = static_cast<json::String const&>(it->second);
	return true;
}

bool export_actor_button_preset(
	agi::fs::path const& path,
	std::vector<ActorButtonDefinition> const& definitions,
	int button_size,
	std::string &error_message)
{
	try {
		json::Object root;
		root["version"] = kActorButtonPresetVersion;
		root["size"] = button_size;

		json::Array buttons;
		for (auto const& input_def : definitions) {
			auto def = input_def;
			normalize_actor_button_definition(def);
			if (def.label.empty() && def.image.empty()) continue;

			json::Object button;
			button["name"] = def.label;

			std::string image_data;
			std::string image_extension;
			if (read_actor_button_image_bytes(def.image, image_data, image_extension)) {
				button["image_extension"] = image_extension;
				button["image_data"] = agi::ass::UUEncode(image_data.data(), image_data.data() + image_data.size(), false);
			}

			buttons.push_back(std::move(button));
		}

		root["buttons"] = std::move(buttons);

		agi::io::Save save(path);
		agi::JsonWriter::Write(root, save.Get());
		return true;
	}
	catch (std::exception const& e) {
		error_message = e.what();
		return false;
	}
}

bool import_actor_button_preset(
	agi::fs::path const& path,
	std::vector<ActorButtonDefinition> &definitions,
	int &button_size,
	std::string &error_message)
{
	try {
		auto root_element = agi::json_util::file(path, "{}");
		auto const& root = static_cast<json::Object const&>(root_element);

		auto size_it = root.find("size");
		if (size_it != root.end())
			button_size = static_cast<int>(static_cast<json::Integer const&>(size_it->second));

		auto buttons_it = root.find("buttons");
		if (buttons_it == root.end())
			throw std::runtime_error("Preset file is missing the buttons array.");

		std::vector<ActorButtonDefinition> imported_definitions;
		for (auto const& item : static_cast<json::Array const&>(buttons_it->second)) {
			auto const& button = static_cast<json::Object const&>(item);

			ActorButtonDefinition def;
			if (!read_json_string(button, "name", def.label))
				read_json_string(button, "label", def.label);
			read_json_string(button, "value", def.value);

			std::string image_data;
			if (read_json_string(button, "image_data", image_data)) {
				std::string image_extension;
				read_json_string(button, "image_extension", image_extension);
				auto decoded = agi::ass::UUDecode(image_data.data(), image_data.data() + image_data.size());
				if (!decoded.empty() || !image_data.empty()) {
					std::string encoded_path;
					if (!save_actor_button_image_bytes(std::string(decoded.begin(), decoded.end()), image_extension, encoded_path))
						throw std::runtime_error("Couldn't restore one of the preset images.");
					def.image = std::move(encoded_path);
				}
			}

			normalize_actor_button_definition(def);
			if (!def.label.empty() || !def.image.empty())
				imported_definitions.push_back(std::move(def));
		}

		definitions = normalize_actor_button_definitions(std::move(imported_definitions));
		return true;
	}
	catch (std::exception const& e) {
		error_message = e.what();
		return false;
	}
}

wxImage get_current_frame_image(agi::Context *c) {
	auto provider = c->project->VideoProvider();
	if (!provider) return wxImage();

	int frame = c->videoController->GetFrameN();
	return GetImage(*provider->GetFrame(frame, c->project->Timecodes().TimeAtFrame(frame), false));
}

class ActorButtonManagerDialog final : public wxDialog {
	agi::Context *c;
	std::vector<ActorButtonDefinition> definitions;

	wxListBox *button_list;
	wxTextCtrl *label_ctrl;
	wxTextCtrl *image_ctrl;
	wxSpinCtrl *size_ctrl;
	wxStaticBitmap *preview_bitmap;
	wxButton *remove_button;
	wxButton *move_up_button;
	wxButton *move_down_button;

	bool updating = false;

	void EnableEditor(bool enabled) {
		label_ctrl->Enable(enabled);
		image_ctrl->Enable(enabled);
		remove_button->Enable(enabled);
		move_up_button->Enable(enabled && button_list->GetSelection() > 0);
		move_down_button->Enable(enabled && button_list->GetSelection() != wxNOT_FOUND && button_list->GetSelection() < int(definitions.size()) - 1);
	}

	void UpdatePreview() {
		const int preview_size = 128;
		wxBitmap bitmap = load_actor_button_bitmap(from_wx(image_ctrl->GetValue()), preview_size);
		if (!bitmap.IsOk())
			bitmap = make_blank_actor_button_bitmap(preview_size);
		preview_bitmap->SetBitmap(bitmap);
	}

	void RefreshList() {
		int selection = button_list->GetSelection();
		button_list->Freeze();
		button_list->Clear();
		for (size_t i = 0; i < definitions.size(); ++i)
			button_list->Append(actor_button_display_name(definitions[i], i));
		button_list->Thaw();

		if (selection != wxNOT_FOUND && selection < int(definitions.size()))
			button_list->SetSelection(selection);
	}

	void UpdateSelectionFromControls() {
		if (updating) return;

		int selection = button_list->GetSelection();
		if (selection == wxNOT_FOUND || selection >= int(definitions.size())) return;

		auto &def = definitions[selection];
		def.label = from_wx(label_ctrl->GetValue());
		def.value = def.label;
		def.image = from_wx(image_ctrl->GetValue());
		button_list->SetString(selection, actor_button_display_name(def, selection));
		EnableEditor(true);
		UpdatePreview();
	}

	void LoadSelectionIntoControls() {
		updating = true;
		int selection = button_list->GetSelection();
		if (selection == wxNOT_FOUND || selection >= int(definitions.size())) {
			label_ctrl->Clear();
			image_ctrl->Clear();
			EnableEditor(false);
			UpdatePreview();
			updating = false;
			return;
		}

		auto const& def = definitions[selection];
		label_ctrl->ChangeValue(to_wx(actor_button_value(def)));
		image_ctrl->ChangeValue(to_wx(def.image));
		EnableEditor(true);
		UpdatePreview();
		updating = false;
	}

	void SelectDefinition(int index) {
		if (index >= 0 && index < int(definitions.size()))
			button_list->SetSelection(index);
		else
			button_list->SetSelection(wxNOT_FOUND);
		LoadSelectionIntoControls();
	}

public:
	ActorButtonManagerDialog(wxWindow *parent, agi::Context *context, std::vector<ActorButtonDefinition> defs, int button_size, bool start_with_new_button = false)
	: wxDialog(parent, -1, _("Actor Buttons"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
	, c(context)
	, definitions(std::move(defs))
	{
		auto *main_sizer = new wxBoxSizer(wxVERTICAL);
		auto *content = new wxBoxSizer(wxHORIZONTAL);
		auto *left_sizer = new wxBoxSizer(wxVERTICAL);
		auto *right_sizer = new wxBoxSizer(wxVERTICAL);

		button_list = new wxListBox(this, -1);
		button_list->SetMinSize(wxSize(220, 260));
		left_sizer->Add(button_list, wxSizerFlags(1).Expand().Border(wxBOTTOM, 8));

		auto *list_buttons = new wxBoxSizer(wxHORIZONTAL);
		auto *add_button = new wxButton(this, -1, _("Add"));
		remove_button = new wxButton(this, -1, _("Remove"));
		move_up_button = new wxButton(this, -1, _("Up"));
		move_down_button = new wxButton(this, -1, _("Down"));
		list_buttons->Add(add_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		list_buttons->Add(remove_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		list_buttons->Add(move_up_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		list_buttons->Add(move_down_button, wxSizerFlags(1));
		left_sizer->Add(list_buttons, wxSizerFlags().Expand());

		auto *preset_buttons = new wxBoxSizer(wxHORIZONTAL);
		auto *import_button = new wxButton(this, -1, _("Import Preset..."));
		auto *export_button = new wxButton(this, -1, _("Export Preset..."));
		preset_buttons->Add(import_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		preset_buttons->Add(export_button, wxSizerFlags(1));
		left_sizer->Add(preset_buttons, wxSizerFlags().Expand().Border(wxTOP, 8));

		auto add_row = [&](wxString const& label, wxWindow *control) {
			auto *row = new wxBoxSizer(wxHORIZONTAL);
			row->Add(new wxStaticText(this, -1, label), wxSizerFlags().Center().Border(wxRIGHT, 8));
			row->Add(control, wxSizerFlags(1).Expand());
			right_sizer->Add(row, wxSizerFlags().Expand().Border(wxBOTTOM, 8));
		};

		size_ctrl = new wxSpinCtrl(this, -1, std::to_wstring(button_size), wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, kActorButtonMinSize, kActorButtonMaxSize, button_size);
		add_row(_("Button size"), size_ctrl);

		label_ctrl = new wxTextCtrl(this, -1);
		add_row(_("Actor name"), label_ctrl);

		image_ctrl = new wxTextCtrl(this, -1);
		add_row(_("Image path"), image_ctrl);

		auto *image_buttons = new wxBoxSizer(wxHORIZONTAL);
		auto *browse_button = new wxButton(this, -1, _("Browse..."));
		auto *clipboard_button = new wxButton(this, -1, _("Paste Clipboard"));
		auto *frame_button = new wxButton(this, -1, _("Use Current Frame"));
		auto *clear_image_button = new wxButton(this, -1, _("Clear Image"));
		image_buttons->Add(browse_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		image_buttons->Add(clipboard_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		image_buttons->Add(frame_button, wxSizerFlags(1).Border(wxRIGHT, 4));
		image_buttons->Add(clear_image_button, wxSizerFlags(1));
		right_sizer->Add(image_buttons, wxSizerFlags().Expand().Border(wxBOTTOM, 8));

		preview_bitmap = new wxStaticBitmap(this, -1, make_blank_actor_button_bitmap(128));
		preview_bitmap->SetMinSize(wxSize(128, 128));
		right_sizer->Add(new wxStaticText(this, -1, _("Preview")), wxSizerFlags().Border(wxBOTTOM, 4));
		right_sizer->Add(preview_bitmap, wxSizerFlags().Border(wxBOTTOM, 4));
		right_sizer->Add(new wxStaticText(this, -1, _("Button images display as square center-crops. Clipboard paste is saved cropped to square.")), wxSizerFlags());

		content->Add(left_sizer, wxSizerFlags(1).Expand().Border(wxRIGHT, 12));
		content->Add(right_sizer, wxSizerFlags(2).Expand());
		main_sizer->Add(content, wxSizerFlags(1).Expand().Border(wxALL, 12));
		main_sizer->Add(CreateSeparatedButtonSizer(wxOK | wxCANCEL), wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 12));

		SetSizerAndFit(main_sizer);
		SetMinSize(wxSize(760, 470));

		button_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent&) { LoadSelectionIntoControls(); });
		label_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { UpdateSelectionFromControls(); });
		image_ctrl->Bind(wxEVT_TEXT, [this](wxCommandEvent&) { UpdateSelectionFromControls(); });
		size_ctrl->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { UpdatePreview(); });

		add_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			size_t index = definitions.size();
			definitions.push_back({
				agi::format("Actor %d", int(index + 1)),
				agi::format("Actor %d", int(index + 1)),
				""
			});
			RefreshList();
			SelectDefinition(int(definitions.size()) - 1);
		});

		remove_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			int selection = button_list->GetSelection();
			if (selection == wxNOT_FOUND || selection >= int(definitions.size())) return;
			definitions.erase(definitions.begin() + selection);
			RefreshList();
			if (definitions.empty())
				SelectDefinition(wxNOT_FOUND);
			else
				SelectDefinition(std::min(selection, int(definitions.size()) - 1));
		});

		move_up_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			int selection = button_list->GetSelection();
			if (selection <= 0 || selection >= int(definitions.size())) return;
			std::swap(definitions[selection], definitions[selection - 1]);
			RefreshList();
			SelectDefinition(selection - 1);
		});

		move_down_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			int selection = button_list->GetSelection();
			if (selection == wxNOT_FOUND || selection >= int(definitions.size()) - 1) return;
			std::swap(definitions[selection], definitions[selection + 1]);
			RefreshList();
			SelectDefinition(selection + 1);
		});

		browse_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			auto filename = OpenFileSelector(_("Choose button image"), "", "", "", "Image files (*.png;*.jpg;*.jpeg;*.bmp;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.webp|All files (*.*)|*.*", this);
			if (!filename.empty())
				image_ctrl->SetValue(to_wx(config::path->Encode(filename)));
		});

		clipboard_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			wxBitmap bitmap;
			if (!read_bitmap_from_clipboard(bitmap)) {
				wxMessageBox(_("The clipboard does not currently contain an image."), _("Clipboard Image"), wxOK | wxICON_INFORMATION, this);
				return;
			}

			std::string encoded_path;
			if (!save_actor_button_image(bitmap.ConvertToImage(), encoded_path, true)) {
				wxMessageBox(_("Couldn't save the clipboard image for this actor button."), _("Clipboard Image"), wxOK | wxICON_ERROR, this);
				return;
			}

			image_ctrl->SetValue(to_wx(encoded_path));
		});

		frame_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			wxImage frame_image = get_current_frame_image(c);
			if (!frame_image.IsOk()) {
				wxMessageBox(_("Open a video first to capture the current frame."), _("Current Frame"), wxOK | wxICON_INFORMATION, this);
				return;
			}

			std::string encoded_path;
			if (!save_actor_button_image(frame_image, encoded_path)) {
				wxMessageBox(_("Couldn't save the current frame for this actor button."), _("Current Frame"), wxOK | wxICON_ERROR, this);
				return;
			}

			image_ctrl->SetValue(to_wx(encoded_path));
		});

		clear_image_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			image_ctrl->Clear();
		});

		import_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			auto filename = OpenFileSelector(_("Import Actor Button Preset"), "", "", "abpreset", "Actor button presets (*.abpreset;*.json)|*.abpreset;*.json|All files (*.*)|*.*", this);
			if (filename.empty()) return;

			std::vector<ActorButtonDefinition> imported_definitions;
			int imported_size = size_ctrl->GetValue();
			std::string error_message;
			if (!import_actor_button_preset(filename, imported_definitions, imported_size, error_message)) {
				wxMessageBox(to_wx(error_message.empty() ? "Couldn't import that preset file." : error_message), _("Import Preset"), wxOK | wxICON_ERROR, this);
				return;
			}

			definitions = std::move(imported_definitions);
			size_ctrl->SetValue(agi::util::mid(kActorButtonMinSize, imported_size, kActorButtonMaxSize));
			RefreshList();
			SelectDefinition(definitions.empty() ? wxNOT_FOUND : 0);
		});

		export_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			auto filename = SaveFileSelector(_("Export Actor Button Preset"), "", "actor-buttons.abpreset", "abpreset", "Actor button presets (*.abpreset)|*.abpreset|JSON files (*.json)|*.json|All files (*.*)|*.*", this);
			if (filename.empty()) return;

			std::string error_message;
			if (!export_actor_button_preset(filename, GetDefinitions(), GetButtonSize(), error_message)) {
				wxMessageBox(to_wx(error_message.empty() ? "Couldn't export that preset file." : error_message), _("Export Preset"), wxOK | wxICON_ERROR, this);
			}
		});

		if (start_with_new_button) {
			size_t index = definitions.size();
			definitions.push_back({
				agi::format("Actor %d", int(index + 1)),
				agi::format("Actor %d", int(index + 1)),
				""
			});
		}

		RefreshList();
		SelectDefinition(definitions.empty() ? wxNOT_FOUND : (start_with_new_button ? int(definitions.size()) - 1 : 0));
	}

	std::vector<ActorButtonDefinition> GetDefinitions() const {
		std::vector<ActorButtonDefinition> out;
		for (auto const& def : definitions) {
			if (!def.label.empty() || !def.value.empty() || !def.image.empty())
				out.push_back(def);
		}
		return out;
	}

	int GetButtonSize() const {
		return size_ctrl->GetValue();
	}
};

/// Work around wxGTK's fondness for generating events from ChangeValue
void change_value(wxTextCtrl *ctrl, wxString const& value) {
	if (value != ctrl->GetValue())
		ctrl->ChangeValue(value);
}

wxString new_value([[maybe_unused]] wxComboBox *ctrl, [[maybe_unused]] wxCommandEvent &evt) {
#ifdef __WXGTK__
	return ctrl->GetValue();
#else
	return evt.GetString();
#endif
}

void time_edit_char_hook(wxKeyEvent &event) {
	// Force a modified event on Enter
	if (event.GetKeyCode() == WXK_RETURN) {
		TimeEdit *edit = static_cast<TimeEdit*>(event.GetEventObject());
		edit->SetValue(edit->GetValue());
	}
	else
		event.Skip();
}

// Passing a pointer-to-member directly to a function sometimes does not work
// in VC++ 2015 Update 2, with it instead passing a null pointer
const auto AssDialogue_Actor = &AssDialogue::Actor;
const auto AssDialogue_Effect = &AssDialogue::Effect;
}

SubsEditBox::SubsEditBox(wxWindow *parent, agi::Context *context)
: wxPanel(parent, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxRAISED_BORDER, "SubsEditBox")
, c(context)
, undo_timer(GetEventHandler())
{
	using std::bind;

	actor_button_bar = new wxBoxSizer(wxHORIZONTAL);
	actor_button_scroller = new wxScrolledWindow(this, -1, wxDefaultPosition, wxDefaultSize, wxVSCROLL | wxTAB_TRAVERSAL);
	actor_button_scroller->SetScrollRate(0, 8);
	actor_button_scroller->EnableScrolling(false, true);
	actor_button_scroller->ShowScrollbars(wxSHOW_SB_NEVER, wxSHOW_SB_DEFAULT);
	actor_button_sizer = new wxWrapSizer(wxHORIZONTAL);
	actor_button_scroller->SetSizer(actor_button_sizer);
	manage_actor_buttons_button = new wxButton(this, -1, _("Edit Buttons"));
	manage_actor_buttons_button->Bind(wxEVT_BUTTON, &SubsEditBox::OnManageActorButtons, this);
	actor_button_bar->Add(actor_button_scroller, wxSizerFlags(1).Expand().Border(wxRIGHT, 8));
	actor_button_bar->Add(manage_actor_buttons_button, wxSizerFlags().Expand());
	RebuildActorButtons();

	// Top controls
	top_sizer = new wxBoxSizer(wxHORIZONTAL);

	comment_box = new wxCheckBox(this,-1,_("&Comment"));
	comment_box->SetToolTip(_("Comment this line out. Commented lines don't show up on screen."));
#ifdef __WXGTK__
	// Only supported in wxgtk
	comment_box->SetCanFocus(false);
#endif
	top_sizer->Add(comment_box, 0, wxRIGHT | wxALIGN_CENTER, 5);

	style_box = MakeComboBox("Default", wxCB_READONLY, &SubsEditBox::OnStyleChange, _("Style for this line"));

	style_edit_button = new wxButton(this, -1, _("Edit"), wxDefaultPosition,
		wxSize(GetTextExtent(_("Edit")).GetWidth() + 20, -1));
	style_edit_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		if (active_style) {
			wxArrayString font_list = wxFontEnumerator::GetFacenames();
			font_list.Sort();
			DialogStyleEditor(this, active_style, c, nullptr, "", font_list).ShowModal();
		}
	});
	top_sizer->Add(style_edit_button, wxSizerFlags().Center().Border(wxRIGHT));

	actor_box = new Placeholder<wxComboBox>(this, _("Actor"), wxSize(110, -1), wxCB_DROPDOWN | wxTE_PROCESS_ENTER, _("Actor name for this speech. This is only for reference, and is mainly useless."));
	Bind(wxEVT_TEXT, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	Bind(wxEVT_COMBOBOX, &SubsEditBox::OnActorChange, this, actor_box->GetId());
	top_sizer->Add(actor_box, wxSizerFlags(2).Center().Border(wxRIGHT));

	effect_box = new Placeholder<wxComboBox>(this, _("Effect"), wxSize(80,-1), wxCB_DROPDOWN | wxTE_PROCESS_ENTER, _("Effect for this line. This can be used to store extra information for karaoke scripts, or for the effects supported by the renderer."));
	Bind(wxEVT_TEXT, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	Bind(wxEVT_COMBOBOX, &SubsEditBox::OnEffectChange, this, effect_box->GetId());
	top_sizer->Add(effect_box, 3, wxALIGN_CENTER, 5);

	char_count = new wxTextCtrl(this, -1, "0", wxDefaultPosition, wxDefaultSize, wxTE_READONLY | wxTE_CENTER);
	char_count->SetInitialSize(char_count->GetSizeFromText(wxS("000")));
	char_count->SetToolTip(_("Number of characters in the longest line of this subtitle."));
	top_sizer->Add(char_count, 0, wxALIGN_CENTER, 5);

	// Middle controls
	middle_left_sizer = new wxBoxSizer(wxHORIZONTAL);

	layer = new wxSpinCtrl(this,-1,"",wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS | wxTE_PROCESS_ENTER,0,999,0);
	layer->SetToolTip(_("Layer number"));
	middle_left_sizer->Add(layer, wxSizerFlags().Center());
	middle_left_sizer->AddSpacer(5);

	start_time = MakeTimeCtrl(_("Start time"), TIME_START);
	end_time   = MakeTimeCtrl(_("End time"), TIME_END);
	middle_left_sizer->AddSpacer(5);
	duration   = MakeTimeCtrl(_("Line duration"), TIME_DURATION);
	middle_left_sizer->AddSpacer(5);

	margin[0] = MakeMarginCtrl(_("Left Margin (0 = default from style)"), 0, _("left margin change"));
	margin[1] = MakeMarginCtrl(_("Right Margin (0 = default from style)"), 1, _("right margin change"));
	margin[2] = MakeMarginCtrl(_("Vertical Margin (0 = default from style)"), 2, _("vertical margin change"));
	middle_left_sizer->AddSpacer(5);

	// Middle-bottom controls
	middle_right_sizer = new wxBoxSizer(wxHORIZONTAL);
	MakeButton("edit/style/bold");
	MakeButton("edit/style/italic");
	MakeButton("edit/style/underline");
	MakeButton("edit/style/strikeout");
	MakeButton("edit/font");
	middle_right_sizer->AddSpacer(5);
	MakeButton("edit/color/primary");
	MakeButton("edit/color/secondary");
	MakeButton("edit/color/outline");
	MakeButton("edit/color/shadow");
	middle_right_sizer->AddSpacer(5);
	MakeButton("grid/line/next/create");
	middle_right_sizer->AddSpacer(10);

	by_time = MakeRadio(_("T&ime"), true, _("Time by h:mm:ss.cs"));
	by_frame = MakeRadio(_("F&rame"), false, _("Time by frame number"));
	by_frame->Enable(false);

	split_box = new wxCheckBox(this,-1,_("Show Original"));
	split_box->SetToolTip(_("Show the contents of the subtitle line when it was first selected above the edit box. This is sometimes useful when editing subtitles or translating subtitles into another language."));
	split_box->Bind(wxEVT_CHECKBOX, &SubsEditBox::OnSplit, this);
	middle_right_sizer->Add(split_box, wxSizerFlags().Center().Left());

	next_line_after_actor_box = new wxCheckBox(this, -1, _("Next line after actor"));
	next_line_after_actor_box->SetToolTip(_("After setting an actor, jump to the next line and play it once."));
	next_line_after_actor_box->SetValue(OPT_GET("Subtitle/Actor Buttons/Next Line After Actor")->GetBool());
	next_line_after_actor_box->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent& evt) {
		bool enabled = !!evt.GetInt();
		OPT_SET("Subtitle/Actor Buttons/Next Line After Actor")->SetBool(enabled);
		if (!enabled)
			CancelActorAdvancePlayback(true);
	});
	middle_right_sizer->Add(next_line_after_actor_box, wxSizerFlags().Center().Left().Border(wxLEFT, 8));

	// Main sizer
	wxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(actor_button_bar, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 3);
	main_sizer->Add(top_sizer,0,wxEXPAND | wxALL,3);
	main_sizer->Add(middle_left_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Add(middle_right_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);

	// Text editor
	edit_ctrl = new SubsTextEditCtrl(this, FromDIP(wxSize(300,50)), wxBORDER_SUNKEN, c);
	edit_ctrl->Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);

	secondary_editor = new wxTextCtrl(this, -1, "", wxDefaultPosition, FromDIP(wxSize(300,50)), wxBORDER_SUNKEN | wxTE_MULTILINE | wxTE_READONLY);

	main_sizer->Add(secondary_editor,1,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Add(edit_ctrl,1,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
	main_sizer->Hide(secondary_editor);

	bottom_sizer = new wxBoxSizer(wxHORIZONTAL);
	bottom_sizer->Add(MakeBottomButton("edit/revert"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/clear/text"), wxSizerFlags().Border(wxRIGHT));
	bottom_sizer->Add(MakeBottomButton("edit/insert_original"));
	main_sizer->Add(bottom_sizer);
	main_sizer->Hide(bottom_sizer);

	SetSizerAndFit(main_sizer);

	edit_ctrl->Bind(wxEVT_STC_MODIFIED, &SubsEditBox::OnChange, this);
	edit_ctrl->SetModEventMask(wxSTC_MOD_INSERTTEXT | wxSTC_MOD_DELETETEXT | wxSTC_STARTACTION);

	Bind(wxEVT_TEXT, &SubsEditBox::OnLayerEnter, this, layer->GetId());
	Bind(wxEVT_SPINCTRL, &SubsEditBox::OnLayerEnter, this, layer->GetId());
	Bind(wxEVT_CHECKBOX, &SubsEditBox::OnCommentChange, this, comment_box->GetId());

	Bind(wxEVT_CHAR_HOOK, &SubsEditBox::OnKeyDown, this);
	Bind(wxEVT_SIZE, &SubsEditBox::OnSize, this);
	Bind(wxEVT_TIMER, [this](wxTimerEvent&) { commit_id = -1; }, undo_timer.GetId());

	wxSizeEvent evt;
	OnSize(evt);

	file_changed_slot = c->ass->AddCommitListener(&SubsEditBox::OnCommit, this);
	connections = agi::signal::make_vector({
		context->project->AddTimecodesListener(&SubsEditBox::UpdateFrameTiming, this),
		context->selectionController->AddActiveLineListener(&SubsEditBox::OnActiveLineChanged, this),
		context->selectionController->AddSelectionListener(&SubsEditBox::OnSelectedSetChanged, this),
		context->initialLineState->AddChangeListener(&SubsEditBox::OnLineInitialTextChanged, this),
		OPT_SUB("Subtitle/Actor Buttons/Definitions", [this](agi::OptionValue const&) { RebuildActorButtons(); }),
		OPT_SUB("Subtitle/Actor Buttons/Size", [this](agi::OptionValue const&) { RebuildActorButtons(); }),
		OPT_SUB("Subtitle/Actor Buttons/Next Line After Actor", [this](agi::OptionValue const& opt) {
			next_line_after_actor_box->SetValue(opt.GetBool());
			if (!opt.GetBool())
				CancelActorAdvancePlayback(true);
		}),
	 });

	context->textSelectionController->SetControl(edit_ctrl);
	edit_ctrl->SetFocus();

	bool show_original = OPT_GET("Subtitle/Show Original")->GetBool();
	if (show_original) {
		split_box->SetValue(true);
		DoOnSplit(true);
	}
}

SubsEditBox::~SubsEditBox() {
	CancelActorAdvancePlayback(false);
	c->textSelectionController->SetControl(nullptr);
}

wxTextCtrl *SubsEditBox::MakeMarginCtrl(wxString const& tooltip, int margin, wxString const& commit_msg) {
	wxTextCtrl *ctrl = new wxTextCtrl(this, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER, IntValidator(0, true));
	ctrl->SetInitialSize(ctrl->GetSizeFromText(wxS("0000")));
	ctrl->SetMaxLength(5);
	ctrl->SetToolTip(tooltip);
	middle_left_sizer->Add(ctrl, wxSizerFlags().Center());

	Bind(wxEVT_TEXT, [=, this](wxCommandEvent&) {
		int value = agi::util::mid(-9999, atoi(ctrl->GetValue().utf8_str()), 99999);
		SetSelectedRows([&](AssDialogue *d) { d->Margin[margin] = value; },
			commit_msg, AssFile::COMMIT_DIAG_META);
	}, ctrl->GetId());

	return ctrl;
}

TimeEdit *SubsEditBox::MakeTimeCtrl(wxString const& tooltip, TimeField field) {
	TimeEdit *ctrl = new TimeEdit(this, -1, c, "", wxDefaultSize, field == TIME_END);
	ctrl->SetInitialSize(ctrl->GetSizeFromText(wxS(" 0:00:00.000 ")));
	ctrl->SetToolTip(tooltip);
	Bind(wxEVT_TEXT, [=, this](wxCommandEvent&) { CommitTimes(field); }, ctrl->GetId());
	ctrl->Bind(wxEVT_CHAR_HOOK, time_edit_char_hook);
	middle_left_sizer->Add(ctrl, wxSizerFlags().Center());
	return ctrl;
}

void SubsEditBox::MakeButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxBitmapButton *btn = new wxBitmapButton(this, -1, command->Icon());
	tool_tip_bindings.emplace_back(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	middle_right_sizer->Add(btn, wxSizerFlags().Expand());
	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
}

wxButton *SubsEditBox::MakeBottomButton(const char *cmd_name) {
	cmd::Command *command = cmd::get(cmd_name);
	wxButton *btn = new wxButton(this, -1, command->StrDisplay(c));
	tool_tip_bindings.emplace_back(btn, command->StrHelp(), "Subtitle Edit Box", cmd_name);

	btn->Bind(wxEVT_BUTTON, std::bind(&SubsEditBox::CallCommand, this, cmd_name));
	return btn;
}

wxComboBox *SubsEditBox::MakeComboBox(wxString const& initial_text, int style, void (SubsEditBox::*handler)(wxCommandEvent&), wxString const& tooltip) {
	wxString styles[] = { "Default" };
	wxComboBox *ctrl = new wxComboBox(this, -1, initial_text, wxDefaultPosition, wxSize(110,-1), 1, styles, style | wxTE_PROCESS_ENTER);
	ctrl->SetToolTip(tooltip);
	top_sizer->Add(ctrl, wxSizerFlags(2).Center().Border(wxRIGHT));
	Bind(wxEVT_COMBOBOX, handler, this, ctrl->GetId());
	return ctrl;
}

wxRadioButton *SubsEditBox::MakeRadio(wxString const& text, bool start, wxString const& tooltip) {
	wxRadioButton *ctrl = new wxRadioButton(this, -1, text, wxDefaultPosition, wxDefaultSize, start ? wxRB_GROUP : 0);
	ctrl->SetValue(start);
	ctrl->SetToolTip(tooltip);
	Bind(wxEVT_RADIOBUTTON, &SubsEditBox::OnFrameTimeRadio, this, ctrl->GetId());
	middle_right_sizer->Add(ctrl, wxSizerFlags().Expand().Border(wxRIGHT));
	return ctrl;
}

void SubsEditBox::OnCommit(int type) {
	wxEventBlocker blocker(this);

	initial_times.clear();

	if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_STYLES) {
		wxString style = style_box->GetValue();
		style_box->Clear();
		style_box->Append(to_wx(c->ass->GetStyles()));
		style_box->Select(style_box->FindString(style));
		active_style = line ? c->ass->GetStyle(line->Style) : nullptr;
	}

	if (type == AssFile::COMMIT_NEW) {
		PopulateList(effect_box, AssDialogue_Effect);
		PopulateList(actor_box, AssDialogue_Actor);
		return;
	}
	else if (type & AssFile::COMMIT_STYLES)
		style_box->Select(style_box->FindString(to_wx(line->Style)));

	if (!(type ^ AssFile::COMMIT_ORDER)) return;

	SetControlsState(!!line);
	UpdateFields(type, true);
}

void SubsEditBox::UpdateFields(int type, bool repopulate_lists) {
	if (!line) return;

	if (type & AssFile::COMMIT_DIAG_TIME) {
		start_time->SetTime(line->Start);
		end_time->SetTime(line->End);
		SetDurationField();
	}

	if (type & AssFile::COMMIT_DIAG_TEXT) {
		edit_ctrl->SetTextTo(line->Text);
		UpdateCharacterCount(line->Text);
	}

	if (type & AssFile::COMMIT_DIAG_META) {
		layer->SetValue(line->Layer);
		for (size_t i = 0; i < margin.size(); ++i)
			change_value(margin[i], std::to_wstring(line->Margin[i]));
		comment_box->SetValue(line->Comment);
		style_box->Select(style_box->FindString(to_wx(line->Style)));
		active_style = line ? c->ass->GetStyle(line->Style) : nullptr;
		style_edit_button->Enable(active_style != nullptr);

		if (repopulate_lists) PopulateList(effect_box, AssDialogue_Effect);
		effect_box->ChangeValue(to_wx(line->Effect));
		effect_box->SetStringSelection(to_wx(line->Effect));

		if (repopulate_lists) PopulateList(actor_box, AssDialogue_Actor);
		actor_box->ChangeValue(to_wx(line->Actor));
		actor_box->SetStringSelection(to_wx(line->Actor));
	}
}

void SubsEditBox::UpdateActorButtons() {
	int bitmap_size = FromDIP(wxSize(OPT_GET("Subtitle/Actor Buttons/Size")->GetInt(), 0)).GetWidth();
	auto definitions = get_actor_button_definitions();

	for (size_t i = 0; i < actor_preset_buttons.size() && i < definitions.size(); ++i) {
		auto *button = actor_preset_buttons[i];
		auto const& def = definitions[i];
		wxString label = actor_button_display_name(def, i);
		wxString actor = to_wx(actor_button_value(def));

		wxBitmap bitmap = load_actor_button_bitmap(def.image, bitmap_size);
		button->SetActorLabel(label);
		button->SetActorBitmap(bitmap);
		button->SetActorToolTip(actor.empty() ? label : fmt_tl("Set actor to %s", actor));
	}
}

void SubsEditBox::RebuildActorButtons() {
	actor_button_sizer->Clear(true);
	actor_preset_buttons.clear();

	int actor_button_size = FromDIP(wxSize(OPT_GET("Subtitle/Actor Buttons/Size")->GetInt(), 0)).GetWidth();
	int actor_tile_height = actor_button_size + FromDIP(54);
	auto definitions = get_actor_button_definitions();

	auto *play_button = new ActorPresetButton(actor_button_scroller, -1, actor_button_size);
	play_button->SetActorLabel(_("Play Line"));
	play_button->SetActorBitmap(make_symbol_actor_button_bitmap(actor_button_size, wxString::FromUTF8("\xE2\x96\xB6")));
	play_button->SetActorToolTip(_("Play the current line again."));
	play_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
		CancelActorAdvancePlayback(true);
		c->videoController->PlayLine();
		edit_ctrl->SetFocus();
	});
	actor_button_sizer->Add(play_button, wxSizerFlags().Expand().Border(wxRIGHT | wxBOTTOM, 6));

	for (size_t i = 0; i < definitions.size(); ++i) {
		auto *button = new ActorPresetButton(actor_button_scroller, -1, actor_button_size);
		button->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { ApplyActorButtonPreset(i); });
		actor_preset_buttons.push_back(button);
		actor_button_sizer->Add(button, wxSizerFlags().Expand().Border(wxRIGHT | wxBOTTOM, 6));
	}

	auto *add_button = new ActorPresetButton(actor_button_scroller, -1, actor_button_size);
	add_button->SetActorLabel(_("Add"));
	add_button->SetActorBitmap(make_symbol_actor_button_bitmap(actor_button_size, "+"));
	add_button->SetActorToolTip(_("Add a new actor button preset."));
	add_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OpenActorButtonManager(true); });
	actor_button_sizer->Add(add_button, wxSizerFlags().Expand().Border(wxRIGHT | wxBOTTOM, 6));

	UpdateActorButtons();
	actor_button_scroller->SetMinSize(wxSize(-1, actor_tile_height * 2 + FromDIP(18)));
	actor_button_scroller->Layout();
	actor_button_scroller->FitInside();
	Layout();
}

void SubsEditBox::OnManageActorButtons(wxCommandEvent &) {
	OpenActorButtonManager(false);
}

void SubsEditBox::OpenActorButtonManager(bool add_new_button) {
	ActorButtonManagerDialog dialog(this, c, get_actor_button_definitions(), OPT_GET("Subtitle/Actor Buttons/Size")->GetInt(), add_new_button);
	if (dialog.ShowModal() != wxID_OK) return;

	OPT_SET("Subtitle/Actor Buttons/Definitions")->SetString(serialize_actor_button_definitions(dialog.GetDefinitions()));
	OPT_SET("Subtitle/Actor Buttons/Size")->SetInt(dialog.GetButtonSize());
	config::opt->Flush();
}

void SubsEditBox::ApplyActorButtonPreset(size_t index) {
	auto definitions = get_actor_button_definitions();
	if (index >= definitions.size()) return;

	wxString actor = to_wx(actor_button_value(definitions[index]));
	if (actor.empty()) return;

	CancelActorAdvancePlayback(true);
	SetSelectedRows(AssDialogue_Actor, actor, _("actor change"), AssFile::COMMIT_DIAG_META);
	actor_box->ChangeValue(actor);
	actor_box->SetStringSelection(actor);
	PopulateList(actor_box, AssDialogue_Actor);

	if (next_line_after_actor_box->GetValue())
		StartActorAdvancePlayback();

	edit_ctrl->SetFocus();
}

void SubsEditBox::StartActorAdvancePlayback() {
	AssDialogue *current_line = c->selectionController->GetActiveLine();
	if (!current_line) return;

	CancelActorAdvancePlayback(true);

	c->selectionController->NextLine();
	AssDialogue *next_line = c->selectionController->GetActiveLine();
	if (!next_line || next_line == current_line) return;

	c->videoController->PlayLine();
	edit_ctrl->SetFocus();
}

void SubsEditBox::CancelActorAdvancePlayback(bool stop_media) {
	if (stop_media) {
		c->videoController->Stop();
		c->audioController->Stop();
	}
}

void SubsEditBox::PopulateList(wxComboBox *combo, boost::flyweight<std::string> AssDialogue::*field) {
	wxEventBlocker blocker(this);

	std::unordered_set<boost::flyweight<std::string>> values;
	for (auto const& line : c->ass->Events) {
		auto const& value = line.*field;
		if (!value.get().empty())
			values.insert(value);
	}

	wxArrayString arrstr;
	arrstr.reserve(values.size());
	transform(values.begin(), values.end(), std::back_inserter(arrstr),
		(wxString (*)(std::string const&))to_wx);

	arrstr.Sort();

	combo->Freeze();
	long pos = combo->GetInsertionPoint();
	wxString value = combo->GetValue();

	combo->Set(arrstr);
	combo->ChangeValue(value);
	combo->SetStringSelection(value);
	combo->SetInsertionPoint(pos);
	combo->Thaw();
}

void SubsEditBox::OnActiveLineChanged(AssDialogue *new_line) {
	wxEventBlocker blocker(this);
	line = new_line;
	commit_id = -1;

	UpdateFields(AssFile::COMMIT_DIAG_FULL, false);
}

void SubsEditBox::OnSelectedSetChanged() {
	initial_times.clear();
}

void SubsEditBox::OnLineInitialTextChanged(std::string const& new_text) {
	if (split_box->IsChecked())
		secondary_editor->SetValue(to_wx(new_text));
}

void SubsEditBox::UpdateFrameTiming(agi::vfr::Framerate const& fps) {
	if (fps.IsLoaded()) {
		by_frame->Enable(true);
	}
	else {
		by_frame->Enable(false);
		by_time->SetValue(true);
		start_time->SetByFrame(false);
		end_time->SetByFrame(false);
		duration->SetByFrame(false);
		c->subsGrid->SetByFrame(false);
	}
}

void SubsEditBox::OnKeyDown(wxKeyEvent &event) {
	if (!osx::ime::process_key_event(edit_ctrl, event))
		hotkey::check("Subtitle Edit Box", c, event);
}

void SubsEditBox::OnChange(wxStyledTextEvent &event) {
	if (line && edit_ctrl->GetTextRaw().data() != line->Text.get()) {
		if (event.GetModificationType() & wxSTC_STARTACTION)
			commit_id = -1;
		CommitText(_("modify text"));
		UpdateCharacterCount(line->Text);
	}
}

void SubsEditBox::Commit(wxString const& desc, int type, bool amend, AssDialogue *line) {
	file_changed_slot.Block();
	commit_id = c->ass->Commit(desc, type, (amend && desc == last_commit_type) ? commit_id : -1, line);
	file_changed_slot.Unblock();
	last_commit_type = desc;
	last_time_commit_type = -1;
	initial_times.clear();
	undo_timer.Start(30000, wxTIMER_ONE_SHOT);
}

template<class setter>
void SubsEditBox::SetSelectedRows(setter set, wxString const& desc, int type, bool amend) {
	auto const& sel = c->selectionController->GetSelectedSet();
	for_each(sel.begin(), sel.end(), set);
	Commit(desc, type, amend, sel.size() == 1 ? *sel.begin() : nullptr);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, T value, wxString const& desc, int type, bool amend) {
	SetSelectedRows([&](AssDialogue *d) { d->*field = value; }, desc, type, amend);
}

template<class T>
void SubsEditBox::SetSelectedRows(T AssDialogueBase::*field, wxString const& value, wxString const& desc, int type, bool amend) {
	boost::flyweight<std::string> conv_value(from_wx(value));
	SetSelectedRows([&](AssDialogue *d) { d->*field = conv_value; }, desc, type, amend);
}

void SubsEditBox::CommitText(wxString const& desc) {
	auto data = edit_ctrl->GetTextRaw();
	SetSelectedRows(&AssDialogue::Text, boost::flyweight<std::string>(data.data(), data.length()), desc, AssFile::COMMIT_DIAG_TEXT, true);
}

void SubsEditBox::CommitTimes(TimeField field) {
	auto const& sel = c->selectionController->GetSelectedSet();
	for (AssDialogue *d : sel) {
		if (!initial_times.count(d))
			initial_times[d] = {d->Start, d->End};

		switch (field) {
			case TIME_START:
				initial_times[d].first = d->Start = start_time->GetTime();
				d->End = std::max(d->Start, initial_times[d].second);
				break;

			case TIME_END:
				initial_times[d].second = d->End = end_time->GetTime();
				d->Start = std::min(d->End, initial_times[d].first);
				break;

			case TIME_DURATION:
				if (by_frame->GetValue()) {
					auto const& fps = c->project->Timecodes();
					d->End = fps.TimeAtFrame(fps.FrameAtTime(d->Start, agi::vfr::START) + duration->GetFrame() - 1, agi::vfr::END);
				}
				else
					d->End = d->Start + duration->GetTime();
				initial_times[d].second = d->End;
				break;
		}
	}

	start_time->SetTime(line->Start);
	end_time->SetTime(line->End);

	if (field != TIME_DURATION)
		SetDurationField();

	if (field != last_time_commit_type)
		commit_id = -1;

	last_time_commit_type = field;
	file_changed_slot.Block();
	commit_id = c->ass->Commit(_("modify times"), AssFile::COMMIT_DIAG_TIME, commit_id, sel.size() == 1 ? *sel.begin() : nullptr);
	file_changed_slot.Unblock();
}

void SubsEditBox::SetDurationField() {
	// With VFR, the frame count calculated from the duration in time can be
	// completely wrong (since the duration is calculated as if it were a start
	// time), so we need to explicitly set it with the correct units.
	if (by_frame->GetValue())
		duration->SetFrame(end_time->GetFrame() - start_time->GetFrame() + 1);
	else
		duration->SetTime(end_time->GetTime() - start_time->GetTime());
}

void SubsEditBox::OnSize(wxSizeEvent &evt) {
	int availableWidth = GetVirtualSize().GetWidth();
	int midMin = middle_left_sizer->GetMinSize().GetWidth();
	int botMin = middle_right_sizer->GetMinSize().GetWidth();

	if (button_bar_split) {
		if (availableWidth > midMin + botMin) {
			GetSizer()->Detach(middle_right_sizer);
			middle_left_sizer->Add(middle_right_sizer,0,wxALIGN_CENTER_VERTICAL);
			button_bar_split = false;
		}
	}
	else {
		if (availableWidth < midMin) {
			middle_left_sizer->Detach(middle_right_sizer);
			GetSizer()->Insert(2,middle_right_sizer,0,wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM,3);
			button_bar_split = true;
		}
	}

	actor_button_scroller->Layout();
	actor_button_scroller->FitInside();

	evt.Skip();
}

void SubsEditBox::OnFrameTimeRadio(wxCommandEvent &event) {
	event.Skip();

	bool byFrame = by_frame->GetValue();
	start_time->SetByFrame(byFrame);
	end_time->SetByFrame(byFrame);
	duration->SetByFrame(byFrame);
	c->subsGrid->SetByFrame(byFrame);

	SetDurationField();
}

void SubsEditBox::SetControlsState(bool state) {
	if (state == controls_enabled) return;
	controls_enabled = state;

	Enable(state);
	if (!state) {
		wxEventBlocker blocker(this);
		edit_ctrl->SetTextTo("");
	}
}

void SubsEditBox::OnSplit(wxCommandEvent&) {
	bool show_original = split_box->IsChecked();
	DoOnSplit(show_original);
	OPT_SET("Subtitle/Show Original")->SetBool(show_original);
}

void SubsEditBox::DoOnSplit(bool show_original) {
	Freeze();
	if (show_original)
		secondary_editor->SetValue(to_wx(c->initialLineState->GetInitialText()));

	GetSizer()->Show(secondary_editor, show_original);
	GetSizer()->Show(bottom_sizer, show_original);
	Fit();
	SetMinSize(GetSize());
	wxSizer* parent_sizer = GetParent()->GetSizer();
	if (parent_sizer) parent_sizer->Layout();
	Thaw();
}

void SubsEditBox::OnStyleChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Style, new_value(style_box, evt), _("style change"), AssFile::COMMIT_DIAG_META);
	active_style = c->ass->GetStyle(line->Style);
}

void SubsEditBox::OnActorChange(wxCommandEvent &evt) {
	bool amend = evt.GetEventType() == wxEVT_TEXT;
	SetSelectedRows(AssDialogue_Actor, new_value(actor_box, evt), _("actor change"), AssFile::COMMIT_DIAG_META, amend);
	PopulateList(actor_box, AssDialogue_Actor);
}

void SubsEditBox::OnLayerEnter(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Layer, evt.GetInt(), _("layer change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::OnEffectChange(wxCommandEvent &evt) {
	bool amend = evt.GetEventType() == wxEVT_TEXT;
	SetSelectedRows(AssDialogue_Effect, new_value(effect_box, evt), _("effect change"), AssFile::COMMIT_DIAG_META, amend);
	PopulateList(effect_box, AssDialogue_Effect);
}

void SubsEditBox::OnCommentChange(wxCommandEvent &evt) {
	SetSelectedRows(&AssDialogue::Comment, !!evt.GetInt(), _("comment change"), AssFile::COMMIT_DIAG_META);
}

void SubsEditBox::CallCommand(const char *cmd_name) {
	cmd::call(cmd_name, c);
	edit_ctrl->SetFocus();
}

void SubsEditBox::UpdateCharacterCount(std::string const& text) {
	int ignore = agi::IGNORE_BLOCKS;
	if (OPT_GET("Subtitle/Character Counter/Ignore Whitespace")->GetBool())
		ignore |= agi::IGNORE_WHITESPACE;
	if (OPT_GET("Subtitle/Character Counter/Ignore Punctuation")->GetBool())
		ignore |= agi::IGNORE_PUNCTUATION;
	size_t length = agi::MaxLineLength(text, ignore);
	char_count->SetValue(std::to_wstring(length));
	size_t limit = (size_t)OPT_GET("Subtitle/Character Limit")->GetInt();
	if (limit && length > limit)
		char_count->SetBackgroundColour(to_wx(OPT_GET("Colour/Subtitle/Syntax/Background/Error")->GetColor()));
	else
		char_count->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
}
