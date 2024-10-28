// -*- mode:c++;indent-tabs-mode:nil;c-basic-offset:4;coding:utf-8 -*-
// vi: set et ft=cpp ts=4 sts=4 sw=4 fenc=utf-8 :vi
//
// Copyright 2024 Mozilla Foundation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "highlight.h"
#include "string.h"
#include <cosmo.h>
#include <ctype.h>

enum {
    NORMAL,
    WORD,
    QUOTE,
    QUOTE_BACKSLASH,
    DQUOTE,
    DQUOTE_BACKSLASH,
    SLASH,
    SLASH_SLASH,
    SLASH_STAR,
    SLASH_STAR_STAR,
    TICK,
    TICK_BACKSLASH,
    TICK_DOLLAR,
    REGEX,
    REGEX_BACKSLASH,
    REGEX_SQUARE,
    REGEX_SQUARE_BACKSLASH,
};

enum {
    EXPECT_VALUE,
    EXPECT_OPERATOR,
};

HighlightTypescript::HighlightTypescript() {
}

HighlightTypescript::~HighlightTypescript() {
}

void HighlightTypescript::feed(std::string *r, std::string_view input) {
    for (size_t i = 0; i < input.size(); ++i) {
        wchar_t c;
        int b = input[i] & 255;
        if (!u_) {
            if (b < 0300) {
                c = b;
            } else {
                c_ = ThomPikeByte(b);
                u_ = ThomPikeLen(b) - 1;
                continue;
            }
        } else if (ThomPikeCont(b)) {
            c = c_ = ThomPikeMerge(c_, b);
            if (--u_)
                continue;
        } else {
            u_ = 0;
            c = b;
        }
        switch (t_) {

        Normal:
        case NORMAL:
            if (!isascii(c) || isalpha(c) || c == '_') {
                t_ = WORD;
                append_wchar(&word_, c);
            } else if (c == '/') {
                t_ = SLASH;
            } else if (c == '\'') {
                t_ = QUOTE;
                *r += HI_STRING;
                *r += '\'';
                expect_ = EXPECT_OPERATOR;
            } else if (c == '"') {
                t_ = DQUOTE;
                *r += HI_STRING;
                *r += '"';
                expect_ = EXPECT_OPERATOR;
            } else if (c == '`') {
                t_ = TICK;
                *r += HI_STRING;
                *r += '`';
                expect_ = EXPECT_OPERATOR;
            } else if (c == '{' && nesti_ && nesti_ < sizeof(nest_)) {
                expect_ = EXPECT_VALUE;
                *r += '{';
                nest_[nesti_++] = NORMAL;
            } else if (c == '}' && nesti_) {
                if ((t_ = nest_[--nesti_]) != NORMAL)
                    *r += HI_STRING;
                *r += '}';
            } else if (c == ')' || c == '}' || c == ']') {
                expect_ = EXPECT_OPERATOR;
                append_wchar(r, c);
            } else if (ispunct(c)) {
                expect_ = EXPECT_VALUE;
                append_wchar(r, c);
            } else if (isdigit(c)) {
                expect_ = EXPECT_OPERATOR;
                append_wchar(r, c);
            } else {
                append_wchar(r, c);
            }
            break;

        Word:
        case WORD:
            if (!isascii(c) || isalnum(c) || c == '_') {
                append_wchar(&word_, c);
            } else {
                if (is_keyword_typescript(word_.data(), word_.size())) {
                    *r += HI_KEYWORD;
                    *r += word_;
                    *r += HI_RESET;
                    expect_ = EXPECT_VALUE;
                } else if (is_keyword_typescript_type(word_.data(), word_.size())) {
                    *r += HI_TYPE;
                    *r += word_;
                    *r += HI_RESET;
                    expect_ = EXPECT_VALUE;
                } else if (is_keyword_js_builtin(word_.data(), word_.size())) {
                    *r += HI_BUILTIN;
                    *r += word_;
                    *r += HI_RESET;
                    expect_ = EXPECT_OPERATOR;
                } else if (is_keyword_js_constant(word_.data(), word_.size())) {
                    *r += HI_CONSTANT;
                    *r += word_;
                    *r += HI_RESET;
                    expect_ = EXPECT_OPERATOR;
                } else {
                    *r += word_;
                    expect_ = EXPECT_OPERATOR;
                }
                word_.clear();
                t_ = NORMAL;
                goto Normal;
            }
            break;

        case SLASH:
            if (c == '/') {
                *r += HI_COMMENT;
                *r += "//";
                t_ = SLASH_SLASH;
            } else if (c == '*') {
                *r += HI_COMMENT;
                *r += "/*";
                t_ = SLASH_STAR;
            } else if (expect_ == EXPECT_VALUE) {
                expect_ = EXPECT_OPERATOR;
                *r += HI_STRING;
                *r += '/';
                append_wchar(r, c);
                if (c == '\\') {
                    t_ = REGEX_BACKSLASH;
                } else if (c == '[') {
                    t_ = REGEX_SQUARE;
                } else {
                    t_ = REGEX;
                }
            } else {
                *r += '/';
                t_ = NORMAL;
                goto Normal;
            }
            break;

        case SLASH_SLASH:
            append_wchar(r, c);
            if (c == '\n') {
                *r += HI_RESET;
                t_ = NORMAL;
            }
            break;

        case SLASH_STAR:
            append_wchar(r, c);
            if (c == '*')
                t_ = SLASH_STAR_STAR;
            break;

        case SLASH_STAR_STAR:
            append_wchar(r, c);
            if (c == '/') {
                *r += HI_RESET;
                t_ = NORMAL;
            } else if (c != '*') {
                t_ = SLASH_STAR;
            }
            break;

        case QUOTE:
            append_wchar(r, c);
            if (c == '\'') {
                *r += HI_RESET;
                t_ = NORMAL;
            } else if (c == '\\') {
                t_ = QUOTE_BACKSLASH;
            }
            break;

        case QUOTE_BACKSLASH:
            append_wchar(r, c);
            t_ = QUOTE;
            break;

        case DQUOTE:
            append_wchar(r, c);
            if (c == '"') {
                *r += HI_RESET;
                t_ = NORMAL;
            } else if (c == '\\') {
                t_ = DQUOTE_BACKSLASH;
            }
            break;

        case DQUOTE_BACKSLASH:
            append_wchar(r, c);
            t_ = DQUOTE;
            break;

        Tick:
        case TICK:
            if (c == '`') {
                *r += '`';
                *r += HI_RESET;
                t_ = NORMAL;
            } else if (c == '$') {
                t_ = TICK_DOLLAR;
            } else if (c == '\\') {
                *r += '\\';
                t_ = TICK_BACKSLASH;
            } else {
                append_wchar(r, c);
            }
            break;

        case TICK_BACKSLASH:
            append_wchar(r, c);
            t_ = TICK;
            break;

        case TICK_DOLLAR:
            if (c == '{' && nesti_ < sizeof(nest_)) {
                // this is how the typescript playground highlights
                *r += HI_RESET;
                *r += '$';
                *r += HI_STRING;
                *r += '{';
                *r += HI_RESET;
                expect_ = EXPECT_VALUE;
                nest_[nesti_++] = TICK;
                t_ = NORMAL;
            } else {
                *r += HI_WARNING;
                *r += '$';
                *r += HI_UNBOLD;
                *r += HI_STRING;
                t_ = TICK;
                goto Tick;
            }
            break;

        case REGEX:
            append_wchar(r, c);
            if (c == '/') {
                *r += HI_RESET;
                t_ = NORMAL;
            } else if (c == '\\') {
                t_ = REGEX_BACKSLASH;
            } else if (c == '[') {
                t_ = REGEX_SQUARE;
            }
            break;

        case REGEX_BACKSLASH:
            append_wchar(r, c);
            t_ = REGEX;
            break;

        case REGEX_SQUARE:
            // because /[/]/g is valid code
            append_wchar(r, c);
            if (c == '\\') {
                t_ = REGEX_SQUARE_BACKSLASH;
            } else if (c == ']') {
                t_ = REGEX;
            }
            break;

        case REGEX_SQUARE_BACKSLASH:
            append_wchar(r, c);
            t_ = REGEX_SQUARE;
            break;

        default:
            __builtin_unreachable();
        }
    }
}

void HighlightTypescript::flush(std::string *r) {
    switch (t_) {
    case WORD:
        if (is_keyword_typescript(word_.data(), word_.size())) {
            *r += HI_KEYWORD;
            *r += word_;
            *r += HI_RESET;
        } else if (is_keyword_typescript_type(word_.data(), word_.size())) {
            *r += HI_TYPE;
            *r += word_;
            *r += HI_RESET;
        } else if (is_keyword_js_builtin(word_.data(), word_.size())) {
            *r += HI_BUILTIN;
            *r += word_;
            *r += HI_RESET;
        } else if (is_keyword_js_constant(word_.data(), word_.size())) {
            *r += HI_CONSTANT;
            *r += word_;
            *r += HI_RESET;
        } else {
            *r += word_;
        }
        word_.clear();
        break;
    case SLASH:
        *r += '/';
        break;
    case TICK_DOLLAR:
        *r += '$';
        *r += HI_RESET;
        break;
    case TICK:
    case TICK_BACKSLASH:
    case QUOTE:
    case QUOTE_BACKSLASH:
    case DQUOTE:
    case DQUOTE_BACKSLASH:
    case SLASH_SLASH:
    case SLASH_STAR:
    case SLASH_STAR_STAR:
    case REGEX:
    case REGEX_BACKSLASH:
    case REGEX_SQUARE:
    case REGEX_SQUARE_BACKSLASH:
        *r += HI_RESET;
        break;
    default:
        break;
    }
    c_ = 0;
    u_ = 0;
    t_ = NORMAL;
}