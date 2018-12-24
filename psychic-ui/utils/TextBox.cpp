/*
 * Copyright 2006 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include <iostream>
#include <cmath>
#include "TextBox.hpp"

namespace psychic_ui {

    TextBox::TextBox() {
        // TODO: Maybe this can be static
        UErrorCode status = U_ZERO_ERROR;
        lineIterator     = std::unique_ptr<icu::BreakIterator>(icu::BreakIterator::createLineInstance(icu::Locale::getDefault(), status));
        wordIterator     = std::unique_ptr<icu::BreakIterator>(icu::BreakIterator::createWordInstance(icu::Locale::getDefault(), status));
        sentenceIterator = std::unique_ptr<icu::BreakIterator>(icu::BreakIterator::createSentenceInstance(icu::Locale::getDefault(), status));
    }

    // region Properties

    void TextBox::setMode(TextBoxMode mode) {
        _mode = mode;
        calculate();
    }

    void TextBox::setAlign(TextBoxAlign align) {
        _align = align;
    }

    void TextBox::setBox(const SkRect &box) {
        _box = box;
    }

    void TextBox::setBox(float left, float top, float right, float bottom) {
        _box.set(left, top, right, bottom);
        calculate();
    }

    void TextBox::setSpacing(float mul, float add) {
        _spacingMult = mul;
        _spacingAdd  = add;
    }

    void TextBox::setPaint(const SkPaint &paint) {
        _paint = &paint;
    }

    // endregion

    void TextBox::setText(const icu::UnicodeString &text) {
        _text = &text;
        updateText();
    }

    void TextBox::updateText() {
        // Note to self: Do not remove this method
        // Even though the iterators kind of work if we modify the text without
        // resetting it, they crash in certain situations
        lineIterator->setText(*_text);
        wordIterator->setText(*_text);
        sentenceIterator->setText(*_text);

        // Text had changed, recalculate line breaks
        calculate();
    }

    void TextBox::calculate() {
        _lineStarts.clear();

        if (_box.width() <= 0 || _text->length() == 0) {
            return;
        }

        unsigned int lastBreak = 0;

        _lineStarts.push_back(lastBreak);

        if (_mode == TextBoxMode::OneLine) {
            return;
        }

        do {
            unsigned int nextBreak = nextLineBreak(lastBreak);

            //if (nextBreak == 0) {
            //    // Skia's breakText broke down, we're probably narrower than a character
            //    // Just assume we're one character wide so that our height is equal to the
            //    // length of the text
            //    count = static_cast<unsigned int>(_text->length());
            //    break;
            //}

            // Index wise there is no difference between the end of string and a
            // final line return, so we have to check unfortunately because we don't
            // want the end of the string being considered as the start of a new line.
            if (nextBreak < _text->length() || _text->charAt(nextBreak - 1) == '\n') {
                _lineStarts.push_back(nextBreak);
            }

            lastBreak = nextBreak;

        } while (lastBreak < _text->length());
    }

    //unsigned int TextBox::countLines() const {
    //    if (_mode == TextBoxMode::OneLine) {
    //        return 1;
    //    }
    //
    //    unsigned int count = 0;
    //    if (_box.width() > 0) {
    //        unsigned int pos = 0;
    //        do {
    //            count += 1;
    //            pos = nextLineBreak(pos);
    //            if (pos == 0) {
    //                // Skia's breakText broke down, we're probably narrower than a character
    //                // Just assume we're one character wide so that our height is equal to the
    //                // length of the text
    //                count = static_cast<unsigned int>(_text->length());
    //                break;
    //            }
    //        } while (pos < _text->length());
    //    }
    //    return count;
    //}

    unsigned int TextBox::lineCount() const {
        return static_cast<unsigned int>(_lineStarts.size());
    }

    float TextBox::getTextHeight() const {
        return _lineStarts.size() * (_paint->getFontSpacing() * _spacingMult + _spacingAdd);
    }

    unsigned int TextBox::nextLineBreak(int start) const {
        icu::UnicodeString remaining = _text->tempSubStringBetween(start, _text->length());
        std::string        str;
        remaining.toUTF8String(str);

        // Start by finding where the text would cut at max if we were not to use UnicodeString
        // TODO: Fix deprecated thing
        SkFont font    = SkFont::LEGACY_ExtractFromPaint(*_paint);
        auto   advance = static_cast<unsigned int>(font.breakText(str.c_str(), str.size(), SkTextEncoding::kUTF8, _box.width()));

        if (_mode == TextBoxMode::OneLine || advance == 0) {
            return advance;
        }

        // Check if a line return exists before that
        auto lineBreak = _text->indexOf('\n', start, advance);
        if (lineBreak != -1) {
            return static_cast<unsigned int>(lineBreak) + 1;
        }

        unsigned int maxBreak = start + advance;
        if (lineIterator->isBoundary(maxBreak)) {
            return maxBreak;
        } else {
            int lastBreak = lineIterator->preceding(maxBreak);
            return lastBreak != icu::BreakIterator::DONE && lastBreak > start ? static_cast<unsigned int>(lastBreak) : maxBreak;
        }
    }

    void TextBox::visit(const TextBoxVisitor &visitor) const {
        float maxWidth = _box.width();

        if (maxWidth <= 0 || _text->length() == 0) {
            return;
        }

        float         x = 0.0f;
        float         y = 0.0f;
        SkFontMetrics metrics{};

        //switch (_paint->getTextAlign()) {
        //    case SkPaint::kLeft_Align:
        //        x = 0;
        //        break;
        //    case SkPaint::kCenter_Align:
        //        x = maxWidth * 0.5f;
        //        break;
        //    default:
        //        x = maxWidth;
        //        break;
        //}

        x += _box.fLeft;

        float fontHeight    = _paint->getFontMetrics(&metrics);
        float scaledSpacing = fontHeight * _spacingMult + _spacingAdd;
        float height        = _box.height();

        // Compute Y position for first line
        float textHeight = fontHeight;
        if (_mode == TextBoxMode::LineBreak && _align != TextBoxAlign::Start) {
            textHeight += scaledSpacing * (_lineStarts.size() - 1);
        }

        switch (_align) {
            case TextBoxAlign::Start:
                y = 0;
                break;
            case TextBoxAlign::Center:
                y = (height - textHeight) * 0.5f;
                break;
            case TextBoxAlign::End:
                y = height - textHeight;
                break;
        }

        y += _box.fTop - metrics.fAscent;

        // Visit lines
        auto lines = static_cast<unsigned int>(_lineStarts.size());

        for (unsigned int i = 0; i < lines; ++i) {
            if (y + metrics.fDescent + metrics.fLeading > 0) {
                std::string str{};
                _text->tempSubStringBetween(
                    _lineStarts[i],
                    i < lines - 1 ? _lineStarts[i + 1] : static_cast<unsigned int>(_text->length())
                ).toUTF8String(str);
                visitor(str.c_str(), str.size(), x, y);
            }


            y += scaledSpacing;

            // NOTE: this was preventing overflow/scroll of text areas
            //if (y + metrics.fAscent >= _box.fBottom) {
            //    break;
            //}

            //lastStart = lineStart;
        }

        // NOTE: This was returning the bottom/height before
        //return y + metrics.fDescent + metrics.fLeading;
    }

    unsigned int TextBox::lineStart(unsigned int line) const {
        if (line >= _lineStarts.size()) {
            return _lineStarts.back();
        } else {
            return _lineStarts[line];
        }
    }

    unsigned int TextBox::lineEnd(unsigned int line) const {
        if (line >= _lineStarts.size() - 1) {
            return static_cast<unsigned int>(_text->length());
        } else {
            return _lineStarts[line + 1] - 1;
        }
    }

    unsigned int TextBox::lineFromIndex(unsigned int index) const {
        unsigned int line = 0;

        for (unsigned int i = 0; i < _lineStarts.size(); ++i) {
            if (index < _lineStarts[i]) {
                break;
            }
            line = i;
        }

        return line;
    }

    std::pair<unsigned int, unsigned int> TextBox::wordAtIndex(unsigned int index) const {
        auto begin = wordIterator->preceding(index);
        auto end   = wordIterator->following(index);
        return std::make_pair(
            begin != icu::BreakIterator::DONE ? begin : 0,
            end != icu::BreakIterator::DONE ? end : _text->length()
        );
    }

    std::pair<unsigned int, unsigned int> TextBox::sentenceAtIndex(unsigned int index) const {
        auto begin = sentenceIterator->preceding(index);
        auto end   = sentenceIterator->following(index);
        return std::make_pair(
            begin != icu::BreakIterator::DONE ? begin : 0,
            end != icu::BreakIterator::DONE ? end : _text->length()
        );
    }

    unsigned int TextBox::previousWordBoundary(unsigned int index) const {
        auto boundary = static_cast<unsigned int>(wordIterator->preceding(index));
        return boundary != icu::BreakIterator::DONE ? boundary : 0;
    }

    unsigned int TextBox::nextWordBoundary(unsigned int index) const {
        auto boundary = static_cast<unsigned int>(wordIterator->following(index));
        return boundary != icu::BreakIterator::DONE ? boundary : static_cast<unsigned int>(_text->length());
    }

    unsigned int TextBox::indexFromPos(int x, int y) const {
        if (_lineStarts.empty()) {
            return 0;
        }

        float lineHeight = _paint->getFontSpacing() * _spacingMult + _spacingAdd;
        auto  line       = static_cast<int>(std::floor((static_cast<float>(y) - _box.fTop) / lineHeight));

        if (line < 0) {
            line = 0;
        } else if (line >= _lineStarts.size()) {
            line = static_cast<int>(_lineStarts.size()) - 1;
        }

        int lineStart = _lineStarts[line];
        int lineEnd   = line < _lineStarts.size() - 1 ? _lineStarts[line + 1] - 1 : static_cast<unsigned int>(_text->length());

        std::string str{};
        _text->tempSubStringBetween(lineStart, lineEnd).toUTF8String(str);

        std::vector<SkScalar> widths(str.size());
        _paint->getTextWidths(str.c_str(), str.size(), &widths.front());
        unsigned int pos    = 0;
        float        xCheck = x + _box.fLeft;
        float        acc    = 0.0f;
        for (pos = 0; pos < widths.size(); ++pos) {
            float width     = widths[pos];
            float halfWidth = width * 0.5f;
            if (xCheck < acc + halfWidth) {
                break;
            }
            acc += width;
        }

        return lineStart + pos;
    }

    std::pair<unsigned int, unsigned int> TextBox::posFromIndex(unsigned int index) const {
        unsigned int line      = 0;
        unsigned int lineStart = 0;

        for (unsigned int i = 0; i < _lineStarts.size(); ++i) {
            if (index < _lineStarts[i]) {
                break;
            }
            line      = i;
            lineStart = _lineStarts[i];
        }

        std::string str{};
        _text->tempSubStringBetween(lineStart, index).toUTF8String(str);
        auto x = static_cast<int>(std::round(_paint->measureText(str.c_str(), str.size())));

        return std::make_pair(line, x);
    }

    // CANVAS VISITOR

    void TextBox::draw(SkCanvas *canvas) {
        visit(
            [this, canvas](const char text[], size_t len, float x, float y) {
                canvas->drawText(text, len, x, y, *_paint);
            }
        );
    }

    // TEXT BLOB VISITOR

    std::unique_ptr<SkTextBlob, std::function<void(SkTextBlob *)>> TextBox::snapshotTextBlob() {
        SkTextBlobBuilder builder{};
        // TODO: Get rid of legacy
        SkFont           font = SkFont::LEGACY_ExtractFromPaint(*_paint);
        //p.setTextEncoding(SkTextEncoding::kGlyphID);
        visit(
            [this, &builder, &font](const char text[], size_t len, float x, float y) {
                _paint->textToGlyphs(text, len, builder.allocRun(font, _paint->countText(text, len), x, y).glyphs);
            }
        );
        return std::unique_ptr<SkTextBlob, std::function<void(SkTextBlob *)>>(builder.make().release(), [](SkTextBlob *ptr) { ptr->unref(); });
    }
}
