/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact:  Qt Software Information (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
**************************************************************************/
/*
  Copyright 2005 Roberto Raggi <roberto@kdevelop.org>

  Permission to use, copy, modify, distribute, and sell this software and its
  documentation for any purpose is hereby granted without fee, provided that
  the above copyright notice appear in all copies and that both that
  copyright notice and this permission notice appear in supporting
  documentation.

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
  KDEVELOP TEAM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
  AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "pp.h"

#include <Lexer.h>
#include <Token.h>
#include <Literals.h>
#include <cctype>

#include <QtDebug>
#include <algorithm>
#include <cctype>

namespace CPlusPlus {

struct Value
{
    enum Kind {
        Kind_Long,
        Kind_ULong,
    };

    Kind kind;

    union {
        long l;
        unsigned long ul;
    };


    Value()
        : kind(Kind_Long), l(0)
    { }

    inline bool is_ulong () const
    { return kind == Kind_ULong; }

    inline void set_ulong (unsigned long v)
    {
        ul = v;
        kind = Kind_ULong;
    }

    inline void set_long (long v)
    {
        l = v;
        kind = Kind_Long;
    }

    inline bool is_zero () const
    { return l == 0; }

#define PP_DEFINE_BIN_OP(name, op) \
    inline Value operator op(const Value &other) const \
    { \
        Value v = *this; \
        if (v.is_ulong () || other.is_ulong ()) \
            v.set_ulong (v.ul op other.ul); \
        else \
            v.set_long (v.l op other.l); \
        return v; \
    }

    PP_DEFINE_BIN_OP(op_add, +)
    PP_DEFINE_BIN_OP(op_sub, -)
    PP_DEFINE_BIN_OP(op_mult, *)
    PP_DEFINE_BIN_OP(op_div, /)
    PP_DEFINE_BIN_OP(op_mod, %)
    PP_DEFINE_BIN_OP(op_lhs, <<)
    PP_DEFINE_BIN_OP(op_rhs, >>)
    PP_DEFINE_BIN_OP(op_lt, <)
    PP_DEFINE_BIN_OP(op_gt, >)
    PP_DEFINE_BIN_OP(op_le, <=)
    PP_DEFINE_BIN_OP(op_ge, >=)
    PP_DEFINE_BIN_OP(op_eq, ==)
    PP_DEFINE_BIN_OP(op_ne, !=)
    PP_DEFINE_BIN_OP(op_bit_and, &)
    PP_DEFINE_BIN_OP(op_bit_or, |)
    PP_DEFINE_BIN_OP(op_bit_xor, ^)
    PP_DEFINE_BIN_OP(op_and, &&)
    PP_DEFINE_BIN_OP(op_or, ||)

#undef PP_DEFINE_BIN_OP
};

} // end of namespace CPlusPlus


using namespace CPlusPlus;


namespace {

class RangeLexer
{
    const Token *first;
    const Token *last;
    Token trivial;

public:
    inline RangeLexer(const Token *first, const Token *last)
        : first(first), last(last)
    {
        // WARN: `last' must be a valid iterator.
        trivial.offset = last->offset;
    }

    inline operator bool() const
    { return first != last; }

    inline bool isValid() const
    { return first != last; }

    inline int size() const
    { return std::distance(first, last); }

    inline const Token *dot() const
    { return first; }

    inline const Token &operator*() const
    {
        if (first != last)
            return *first;

        return trivial;
    }

    inline const Token *operator->() const
    {
        if (first != last)
            return first;

        return &trivial;
    }

    inline RangeLexer &operator++()
    {
        ++first;
        return *this;
    }
};

class ExpressionEvaluator
{
    ExpressionEvaluator(const ExpressionEvaluator &other);
    void operator = (const ExpressionEvaluator &other);

public:
    ExpressionEvaluator(Environment *env)
        : env(env), _lex(0)
    { }

    Value operator()(const Token *firstToken, const Token *lastToken,
                     const QByteArray &source)
    {
        this->source = source;
        const Value previousValue = switchValue(Value());
        RangeLexer tmp(firstToken, lastToken);
        RangeLexer *previousLex = _lex;
        _lex = &tmp;
        process_expression();
        _lex = previousLex;
        return switchValue(previousValue);
    }

protected:
    Value switchValue(const Value &value)
    {
        Value previousValue = _value;
        _value = value;
        return previousValue;
    }

    bool isTokenDefined() const
    {
        if ((*_lex)->isNot(T_IDENTIFIER))
            return false;
        const QByteArray spell = tokenSpell();
        if (spell.size() != 7)
            return false;
        return spell == "defined";
    }

    QByteArray tokenSpell() const
    {
        const QByteArray text = QByteArray::fromRawData(source.constData() + (*_lex)->offset,
                                                        (*_lex)->length);
        return text;
    }

    bool process_expression()
    { return process_constant_expression(); }

    bool process_primary()
    {
        if ((*_lex)->is(T_INT_LITERAL)) {
            int base = 10;
            const QByteArray spell = tokenSpell();
            if (spell.at(0) == '0') {
                if (spell.size() > 1 && (spell.at(1) == 'x' || spell.at(1) == 'X'))
                    base = 16;
                else
                    base = 8;
            }
            _value.set_long(tokenSpell().toLong(0, base));
            ++(*_lex);
            return true;
        } else if (isTokenDefined()) {
            ++(*_lex);
            if ((*_lex)->is(T_IDENTIFIER)) {
                _value.set_long(env->resolve(tokenSpell()) != 0);
                ++(*_lex);
                return true;
            } else if ((*_lex)->is(T_LPAREN)) {
                ++(*_lex);
                if ((*_lex)->is(T_IDENTIFIER)) {
                    _value.set_long(env->resolve(tokenSpell()) != 0);
                    ++(*_lex);
                    if ((*_lex)->is(T_RPAREN)) {
                        ++(*_lex);
                        return true;
                    }
                }
                return false;
            }
            return true;
        } else if ((*_lex)->is(T_IDENTIFIER)) {
            _value.set_long(0);
            ++(*_lex);
            return true;
        } else if ((*_lex)->is(T_MINUS)) {
            ++(*_lex);
            process_primary();
            _value.set_long(- _value.l);
            return true;
        } else if ((*_lex)->is(T_PLUS)) {
            ++(*_lex);
            process_primary();
            return true;
        } else if ((*_lex)->is(T_EXCLAIM)) {
            ++(*_lex);
            process_primary();
            _value.set_long(_value.is_zero());
            return true;
        } else if ((*_lex)->is(T_LPAREN)) {
            ++(*_lex);
            process_expression();
            if ((*_lex)->is(T_RPAREN))
                ++(*_lex);
            return true;
        }

        return false;
    }

    bool process_multiplicative()
    {
        process_primary();

        while ((*_lex)->is(T_STAR) || (*_lex)->is(T_SLASH) || (*_lex)->is(T_PERCENT)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_primary();

            if (op.is(T_STAR)) {
                _value = left * _value;
            } else if (op.is(T_SLASH)) {
                if (_value.is_zero())
                    _value.set_long(0);
                else
                    _value = left / _value;
            } else if (op.is(T_PERCENT)) {
                if (_value.is_zero())
                    _value.set_long(0);
                else
                    _value = left % _value;
            }
        }

        return true;
    }

    bool process_additive()
    {
        process_multiplicative();

        while ((*_lex)->is(T_PLUS) || (*_lex)->is(T_MINUS)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_multiplicative();

            if (op.is(T_PLUS))
                _value = left + _value;
            else if (op.is(T_MINUS))
                _value = left - _value;
        }

        return true;
    }

    bool process_shift()
    {
        process_additive();

        while ((*_lex)->is(T_MINUS_MINUS) || (*_lex)->is(T_GREATER_GREATER)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_additive();

            if (op.is(T_MINUS_MINUS))
                _value = left << _value;
            else if (op.is(T_GREATER_GREATER))
                _value = left >> _value;
        }

        return true;
    }

    bool process_relational()
    {
        process_shift();

        while ((*_lex)->is(T_LESS)    || (*_lex)->is(T_LESS_EQUAL) ||
               (*_lex)->is(T_GREATER) || (*_lex)->is(T_GREATER_EQUAL)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_shift();

            if (op.is(T_LESS))
                _value = left < _value;
            else if (op.is(T_LESS_EQUAL))
                _value = left <= _value;
            else if (op.is(T_GREATER))
                _value = left > _value;
            else if (op.is(T_GREATER_EQUAL))
                _value = left >= _value;
        }

        return true;
    }

    bool process_equality()
    {
        process_relational();

        while ((*_lex)->is(T_EXCLAIM_EQUAL) || (*_lex)->is(T_EQUAL_EQUAL)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_relational();

            if (op.is(T_EXCLAIM_EQUAL))
                _value = left != _value;
            else if (op.is(T_EQUAL_EQUAL))
                _value = left == _value;
        }

        return true;
    }

    bool process_and()
    {
        process_equality();

        while ((*_lex)->is(T_AMPER)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_equality();

            _value = left & _value;
        }

        return true;
    }

    bool process_xor()
    {
        process_and();

        while ((*_lex)->is(T_CARET)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_and();

            _value = left ^ _value;
        }

        return true;
    }

    bool process_or()
    {
        process_xor();

        while ((*_lex)->is(T_PIPE)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_xor();

            _value = left | _value;
        }

        return true;
    }

    bool process_logical_and()
    {
        process_or();

        while ((*_lex)->is(T_AMPER_AMPER)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_or();

            _value = left && _value;
        }

        return true;
    }

    bool process_logical_or()
    {
        process_logical_and();

        while ((*_lex)->is(T_PIPE_PIPE)) {
            const Token op = *(*_lex);
            ++(*_lex);

            const Value left = _value;
            process_logical_and();

            _value = left || _value;
        }

        return true;
    }

    bool process_constant_expression()
    {
        process_logical_or();
        const Value cond = _value;
        if ((*_lex)->is(T_QUESTION)) {
            ++(*_lex);
            process_constant_expression();
            Value left = _value, right;
            if ((*_lex)->is(T_COLON)) {
                ++(*_lex);
                process_constant_expression();
                right = _value;
            }
            _value = ! cond.is_zero() ? left : right;
        }

        return true;
    }

private:
    Environment *env;
    QByteArray source;
    RangeLexer *_lex;
    Value _value;
};

} // end of anonymous namespace


Preprocessor::Preprocessor(Client *client, Environment *env)
    : client(client),
      env(env),
      _expand(env),
      _result(0),
      _markGeneratedTokens(false)
{
    resetIfLevel ();
}

void Preprocessor::pushState(const State &s)
{
    _savedStates.append(state());
    _source = s.source;
    _tokens = s.tokens;
    _dot = s.dot;
}

Preprocessor::State Preprocessor::state() const
{
    State state;
    state.source = _source;
    state.tokens = _tokens;
    state.dot = _dot;
    return state;
}

void Preprocessor::popState()
{
    const State &state = _savedStates.last();
    _source = state.source;
    _tokens = state.tokens;
    _dot = state.dot;
    _savedStates.removeLast();
}

QByteArray Preprocessor::operator()(const QByteArray &filename,
                                    const QByteArray &source)
{
    QByteArray preprocessed;
    preprocess(filename, source, &preprocessed);
    return preprocessed;
}

QByteArray Preprocessor::expand(const QByteArray &source)
{
    QByteArray result;
    result.reserve(256);
    expand(source, &result);
    return result;
}

void Preprocessor::expand(const QByteArray &source, QByteArray *result)
{
    if (result)
        _expand(source, result);
}

void Preprocessor::expand(const char *first, const char *last, QByteArray *result)
{
    const QByteArray source = QByteArray::fromRawData(first, last - first);
    return expand(source, result);
}

void Preprocessor::out(const QByteArray &text)
{
    if (_result)
        _result->append(text);
}

void Preprocessor::out(char ch)
{
    if (_result)
        _result->append(ch);
}

void Preprocessor::out(const char *s)
{
    if (_result)
        _result->append(s);
}

Preprocessor::State Preprocessor::createStateFromSource(const QByteArray &source) const
{
    State state;
    state.source = source;
    Lexer lex(state.source.constBegin(), state.source.constEnd());
    lex.setScanKeywords(false);
    Token tok;
    do {
        lex(&tok);
        state.tokens.append(tok);
    } while (tok.isNot(T_EOF_SYMBOL));
    state.dot = state.tokens.constBegin();
    return state;
}

void Preprocessor::processNewline(bool force)
{
    if (! force && env->currentLine == _dot->lineno)
        return;

    if (force || env->currentLine > _dot->lineno) {
        out("\n# ");
        out(QByteArray::number(_dot->lineno));
        out(' ');
        out('"');
        out(env->currentFile);
        out('"');
        out('\n');
    } else {
        for (unsigned i = env->currentLine; i < _dot->lineno; ++i)
            out('\n');
    }

    env->currentLine = _dot->lineno;
}

void Preprocessor::processSkippingBlocks(bool skippingBlocks,
                                         TokenIterator start, TokenIterator /*end*/)
{
    if (! client)
        return;

    if (skippingBlocks != _skipping[iflevel]) {
        unsigned offset = start->offset;

        if (_skipping[iflevel]) {
            if (_dot->newline)
                ++offset;

            client->startSkippingBlocks(offset);

        } else {
            if (offset)
                --offset;

            client->stopSkippingBlocks(offset);
        }
    }
}

bool Preprocessor::markGeneratedTokens(bool markGeneratedTokens,
                                       TokenIterator dot)
{
    bool previous = _markGeneratedTokens;
    _markGeneratedTokens = markGeneratedTokens;

    if (previous != _markGeneratedTokens) {
        if (! dot)
            dot = _dot;

        if (_markGeneratedTokens)
            out("\n#gen true");
        else
            out("\n#gen false");

        processNewline(/*force = */ true);

        const char *begin = _source.constBegin();
        const char *end   = begin;

        if (markGeneratedTokens)
            end += dot->begin();
        else
            end += (dot - 1)->end();

        const char *it = end - 1;
        for (; it != begin - 1; --it) {
            if (*it == '\n')
                break;
        }
        ++it;

        for (; it != end; ++it) {
            if (! std::isspace(*it))
                out(' ');

            else
                out(*it);
        }
    }

    return previous;
}

void Preprocessor::preprocess(const QByteArray &fileName, const QByteArray &source,
                              QByteArray *result)
{
    QByteArray *previousResult = _result;
    _result = result;

    pushState(createStateFromSource(source));

    const QByteArray previousFileName = env->currentFile;
    env->currentFile = fileName;

    const unsigned previousCurrentLine = env->currentLine;
    env->currentLine = 0;

    while (true) {
        processNewline();

        if (_dot->is(T_EOF_SYMBOL)) {
            break;

        } else if (_dot->is(T_POUND) && (! _dot->joined && _dot->newline)) {
            // handle the preprocessor directive

            TokenIterator start = _dot;
            do {
                ++_dot;
            } while (_dot->isNot(T_EOF_SYMBOL) && (_dot->joined || ! _dot->newline));

            const bool skippingBlocks = _skipping[iflevel];

            processDirective(start, _dot);
            processSkippingBlocks(skippingBlocks, start, _dot);

        } else if (skipping()) {
            // skip the current line

            do {
                ++_dot;
            } while (_dot->isNot(T_EOF_SYMBOL) && (_dot->joined || ! _dot->newline));

        } else {

            if (_dot->joined)
                out("\\\n");

            else if (_dot->whitespace) {
                const unsigned endOfPreviousToken = (_dot - 1)->end();
                const unsigned beginOfToken = _dot->begin();

                const char *start = _source.constBegin() + endOfPreviousToken;
                const char *end = _source.constBegin() + beginOfToken;

                const char *it = end - 1;
                for (; it != start - 1; --it) {
                    if (*it == '\n')
                        break;
                }
                ++it;

                for (; it != end; ++it) {
                    if (std::isspace(*it))
                        out(*it);

                    else
                        out(' ');
                }
            }

            if (_dot->isNot(T_IDENTIFIER)) {
                out(tokenSpell(*_dot));
                ++_dot;

            } else {
                const TokenIterator identifierToken = _dot;
                ++_dot; // skip T_IDENTIFIER

                const QByteArray spell = tokenSpell(*identifierToken);

                if (env->isBuiltinMacro(spell))
                    expandBuiltinMacro(identifierToken, spell);

                else {
                    if (Macro *m = env->resolve(spell)) {
                        if (! m->isFunctionLike()) {
                            if (0 == (m = processObjectLikeMacro(identifierToken, spell, m)))
                                continue;

                            // the macro expansion generated something that looks like
                            // a function-like macro.
                        }

                        // `m' is function-like macro.
                        if (_dot->is(T_LPAREN)) {
                            QVector<MacroArgumentReference> actuals;
                            collectActualArguments(&actuals);

                            if (_dot->is(T_RPAREN)) {
                                expandFunctionLikeMacro(identifierToken, m, actuals);
                                continue;
                            }
                        }
                    }

                    // it's not a function or object-like macro.
                    out(spell);
                }
            }
        }
    }

    popState();

    env->currentFile = previousFileName;
    env->currentLine = previousCurrentLine;
    _result = previousResult;
}

void Preprocessor::collectActualArguments(QVector<MacroArgumentReference> *actuals)
{
    if (_dot->isNot(T_LPAREN))
        return;

    ++_dot;

    if (_dot->is(T_RPAREN))
        return;

    actuals->append(collectOneActualArgument());

    while (_dot->is(T_COMMA)) {
        ++_dot;

        actuals->append(collectOneActualArgument());
    }
}

MacroArgumentReference Preprocessor::collectOneActualArgument()
{
    const unsigned position = _dot->begin();

    while (_dot->isNot(T_EOF_SYMBOL)) {
        if (_dot->is(T_COMMA) || _dot->is(T_RPAREN))
            break;

        if (_dot->isNot(T_LPAREN))
            ++_dot;

        else {
            int count = 0;

            for (; _dot->isNot(T_EOF_SYMBOL); ++_dot) {
                if (_dot->is(T_LPAREN))
                    ++count;

                else if (_dot->is(T_RPAREN)) {
                    if (! --count) {
                        ++_dot;
                        break;
                    }
                }
            }
        }
    }

    const unsigned end = _dot->begin();

    return MacroArgumentReference(position, end - position);
}

Macro *Preprocessor::processObjectLikeMacro(TokenIterator identifierToken,
                                            const QByteArray &spell,
                                            Macro *m)
{
    QByteArray tmp;
    expandObjectLikeMacro(identifierToken, spell, m, &tmp);

    if (_dot->is(T_LPAREN)) {
        // check if the expension generated a function-like macro.

        m = 0; // reset the active the macro

        pushState(createStateFromSource(tmp));

        if (_dot->is(T_IDENTIFIER)) {
            const QByteArray id = tokenSpell(*_dot);

            if (Macro *macro = env->resolve(id)) {
                if (macro->isFunctionLike())
                    m = macro;
            }
        }

        popState();

        if (m != 0)
            return m;
    }

    const bool was = markGeneratedTokens(true, identifierToken);
    out(tmp);
    (void) markGeneratedTokens(was);
    return 0;
}

void Preprocessor::expandBuiltinMacro(TokenIterator identifierToken,
                                      const QByteArray &spell)
{
    const Macro trivial;

    if (client)
        client->startExpandingMacro(identifierToken->offset,
                                    trivial, spell);

    const bool was = markGeneratedTokens(true, identifierToken);
    expand(spell, _result);
    (void) markGeneratedTokens(was);

    if (client)
        client->stopExpandingMacro(_dot->offset, trivial);
}

void Preprocessor::expandObjectLikeMacro(TokenIterator identifierToken,
                                         const QByteArray &spell,
                                         Macro *m,
                                         QByteArray *result)
{
    if (client)
        client->startExpandingMacro(identifierToken->offset,
                                    *m, spell);

    m->setHidden(true);
    expand(m->definition(), result);
    m->setHidden(false);

    if (client)
        client->stopExpandingMacro(_dot->offset, *m);
}

void Preprocessor::expandFunctionLikeMacro(TokenIterator identifierToken,
                                           Macro *m,
                                           const QVector<MacroArgumentReference> &actuals)
{
    const char *beginOfText = startOfToken(*identifierToken);
    const char *endOfText = endOfToken(*_dot);
    ++_dot; // skip T_RPAREN

    if (client) {
        const QByteArray text =
                QByteArray::fromRawData(beginOfText,
                                        endOfText - beginOfText);

        client->startExpandingMacro(identifierToken->offset,
                                    *m, text, actuals);
    }

    const bool was = markGeneratedTokens(true, identifierToken);
    expand(beginOfText, endOfText, _result);
    (void) markGeneratedTokens(was);

    if (client)
        client->stopExpandingMacro(_dot->offset, *m);
}

const char *Preprocessor::startOfToken(const Token &token) const
{ return _source.constBegin() + token.begin(); }

const char *Preprocessor::endOfToken(const Token &token) const
{ return _source.constBegin() + token.end(); }

QByteArray Preprocessor::tokenSpell(const Token &token) const
{
    const QByteArray text = QByteArray::fromRawData(_source.constBegin() + token.offset,
                                                     token.length);
    return text;
}

QByteArray Preprocessor::tokenText(const Token &token) const
{
    const QByteArray text(_source.constBegin() + token.offset,
                          token.length);
    return text;
}

void Preprocessor::processDirective(TokenIterator firstToken, TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);
    ++tk; // skip T_POUND

    if (tk->is(T_IDENTIFIER)) {
        const QByteArray directive = tokenSpell(*tk);
        switch (PP_DIRECTIVE_TYPE d = classifyDirective(directive)) {
        case PP_DEFINE:
            if (! skipping())
                processDefine(firstToken, lastToken);
            break;

        case PP_INCLUDE:
        case PP_INCLUDE_NEXT:
        case PP_IMPORT:
            if (! skipping())
                processInclude(d == PP_INCLUDE_NEXT, firstToken, lastToken);
            break;

        case PP_UNDEF:
            if (! skipping())
                processUndef(firstToken, lastToken);
            break;

        case PP_ELIF:
            processElif(firstToken, lastToken);
            break;

        case PP_ELSE:
            processElse(firstToken, lastToken);
            break;

        case PP_ENDIF:
            processEndif(firstToken, lastToken);
            break;

        case PP_IF:
            processIf(firstToken, lastToken);
            break;

        case PP_IFDEF:
        case PP_IFNDEF:
            processIfdef(d == PP_IFNDEF, firstToken, lastToken);
            break;

        default:
            break;
        } // switch
    }
}

QVector<Token> Preprocessor::tokenize(const QByteArray &text) const
{
    QVector<Token> tokens;
    Lexer lex(text.constBegin(), text.constEnd());
    lex.setScanKeywords(false);
    Token tk;
    do {
        lex(&tk);
        tokens.append(tk);
    } while (tk.isNot(T_EOF_SYMBOL));
    return tokens;
}

void Preprocessor::processInclude(bool, TokenIterator firstToken,
                                  TokenIterator lastToken, bool acceptMacros)
{
    if (! client)
        return; // nothing to do.

    RangeLexer tk(firstToken, lastToken);
    ++tk; // skip T_POUND
    ++tk; // skip `include|nclude_next'

    if (acceptMacros && tk->is(T_IDENTIFIER)) {
        // ### TODO: implement me
#if 0
        QByteArray name;
        name.reserve(256);
        MacroExpander expandInclude(env);
        expandInclude(startOfToken(tokens.at(2)),
                      startOfToken(tokens.last()),
                      &name);
        const QByteArray previousSource = switchSource(name);
        //processInclude(skipCurentPath, tokenize(name), /*accept macros=*/ false);
        (void) switchSource(previousSource);
#endif

    } else if (tk->is(T_LESS)) {

        TokenIterator start = tk.dot();

        for (; tk->isNot(T_EOF_SYMBOL); ++tk) {
            if (tk->is(T_GREATER))
                break;
        }

        const char *beginOfPath = endOfToken(*start);
        const char *endOfPath = startOfToken(*tk);

        QString fn = QString::fromUtf8(beginOfPath, endOfPath - beginOfPath);
        client->sourceNeeded(fn, Client::IncludeGlobal, firstToken->lineno);

    } else if (tk->is(T_ANGLE_STRING_LITERAL) || tk->is(T_STRING_LITERAL)) {

        const QByteArray spell = tokenSpell(*tk);
        const char *beginOfPath = spell.constBegin();
        const char *endOfPath = spell.constEnd();
        const char quote = *beginOfPath;

        if (beginOfPath + 1 != endOfPath && ((quote == '"' && endOfPath[-1] == '"') ||
                                              (quote == '<' && endOfPath[-1] == '>'))) {

            QString fn = QString::fromUtf8(beginOfPath + 1, spell.length() - 2);
            client->sourceNeeded(fn, Client::IncludeLocal, firstToken->lineno);
        }
    }
}

void Preprocessor::processDefine(TokenIterator firstToken, TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);

    if (tk.size() < 3)
        return; // nothing to do

    ++tk; // skip T_POUND
    ++tk; // skip T_DEFINE

    if (tk->isNot(T_IDENTIFIER)) {
        // ### warning expected an `identifier'
        return;
    }

    Macro macro;
    macro.setFileName(env->currentFile);
    macro.setLine(env->currentLine);
    macro.setName(tokenText(*tk));
    ++tk; // skip T_IDENTIFIER

    if (tk->is(T_LPAREN) && ! tk->whitespace) {
        // a function-like macro definition
        macro.setFunctionLike(true);

        ++tk; // skip T_LPAREN
        if (tk->is(T_IDENTIFIER)) {
            macro.addFormal(tokenText(*tk));
            ++tk; // skip T_IDENTIFIER
            while (tk->is(T_COMMA)) {
                ++tk;// skip T_COMMA
                if (tk->isNot(T_IDENTIFIER))
                    break;
                macro.addFormal(tokenText(*tk));
                ++tk; // skip T_IDENTIFIER
            }
        }

        if (tk->is(T_DOT_DOT_DOT)) {
            macro.setVariadic(true);
            ++tk; // skip T_DOT_DOT_DOT
        }

        if (tk->isNot(T_RPAREN)) {
            // ### warning expected `)'
            return;
        }

        ++tk; // skip T_RPAREN
    }

    if (isQtReservedWord(macro.name())) {
        QByteArray macroId = macro.name();

        if (macro.isFunctionLike()) {
            macroId += '(';
            bool fst = true;
            foreach (const QByteArray formal, macro.formals()) {
                if (! fst)
                    macroId += ", ";
                fst = false;
                macroId += formal;
            }
            macroId += ')';
        }

        macro.setDefinition(macroId);
    } else {
        // ### make me fast!
        const char *startOfDefinition = startOfToken(*tk);
        const char *endOfDefinition = startOfToken(*lastToken);
        QByteArray definition(startOfDefinition,
                              endOfDefinition - startOfDefinition);
        definition.replace("\\\n", " ");
        definition.replace('\n', ' ');
        macro.setDefinition(definition.trimmed());
    }

    env->bind(macro);

    if (client)
        client->macroAdded(macro);
}

void Preprocessor::processIf(TokenIterator firstToken, TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);

    ++tk; // skip T_POUND
    ++tk; // skipt `if'

    if (testIfLevel()) {
        const char *first = startOfToken(*tk);
        const char *last = startOfToken(*lastToken);

        MacroExpander expandCondition (env);
        QByteArray condition;
        condition.reserve(256);
        expandCondition(first, last, &condition);

        QVector<Token> tokens = tokenize(condition);

        const Value result = evalExpression(tokens.constBegin(),
                                            tokens.constEnd() - 1,
                                            condition);

        _true_test[iflevel] = ! result.is_zero ();
        _skipping[iflevel]  =   result.is_zero ();
    }
}

void Preprocessor::processElse(TokenIterator firstToken, TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);

    if (iflevel == 0 && !skipping ()) {
        // std::cerr << "*** WARNING #else without #if" << std::endl;
    } else if (iflevel > 0 && _skipping[iflevel - 1]) {
        _skipping[iflevel] = true;
    } else {
        _skipping[iflevel] = _true_test[iflevel];
    }
}

void Preprocessor::processElif(TokenIterator firstToken, TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);
    ++tk; // skip T_POUND
    ++tk; // skipt `elif'

    if (! (iflevel > 0)) {
        // std::cerr << "*** WARNING: " << __FILE__ << __LINE__ << std::endl;
    } else if (iflevel == 0 && !skipping()) {
        // std::cerr << "*** WARNING #else without #if" << std::endl;
    } else if (!_true_test[iflevel] && !_skipping[iflevel - 1]) {

        const char *first = startOfToken(*tk);
        const char *last = startOfToken(*lastToken);

        MacroExpander expandCondition (env);
        QByteArray condition;
        condition.reserve(256);
        expandCondition(first, last, &condition);

        QVector<Token> tokens = tokenize(condition);

        const Value result = evalExpression(tokens.constBegin(),
                                            tokens.constEnd() - 1,
                                            condition);

        _true_test[iflevel] = ! result.is_zero ();
        _skipping[iflevel]  =   result.is_zero ();
    } else {
        _skipping[iflevel] = true;
    }
}

void Preprocessor::processEndif(TokenIterator, TokenIterator)
{
    if (iflevel == 0 && !skipping()) {
        // std::cerr << "*** WARNING #endif without #if" << std::endl;
    } else {
        _skipping[iflevel] = false;
        _true_test[iflevel] = false;

        --iflevel;
    }
}

void Preprocessor::processIfdef(bool checkUndefined,
                                TokenIterator firstToken,
                                TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);

    ++tk; // skip T_POUND
    ++tk; // skip `ifdef'
    if (testIfLevel()) {
        if (tk->is(T_IDENTIFIER)) {
            const QByteArray macroName = tokenSpell(*tk);
            bool value = env->resolve(macroName) != 0 || env->isBuiltinMacro(macroName);

            if (checkUndefined)
                value = ! value;

            _true_test[iflevel] =   value;
            _skipping [iflevel] = ! value;
        }
    }
}

void Preprocessor::processUndef(TokenIterator firstToken, TokenIterator lastToken)
{
    RangeLexer tk(firstToken, lastToken);

    ++tk; // skip T_POUND
    ++tk; // skip `undef'

    if (tk->is(T_IDENTIFIER)) {
        const QByteArray macroName = tokenText(*tk);
        const Macro *macro = env->remove(macroName);

        if (client && macro)
            client->macroAdded(*macro);
    }
}

void Preprocessor::resetIfLevel ()
{
    iflevel = 0;
    _skipping[iflevel] = false;
    _true_test[iflevel] = false;
}

Preprocessor::PP_DIRECTIVE_TYPE Preprocessor::classifyDirective(const QByteArray &directive) const
{
    switch (directive.size())
    {
    case 2:
        if (directive[0] == 'i' && directive[1] == 'f')
            return PP_IF;
        break;

    case 4:
        if (directive[0] == 'e' && directive == "elif")
            return PP_ELIF;
        else if (directive[0] == 'e' && directive == "else")
            return PP_ELSE;
        break;

    case 5:
        if (directive[0] == 'i' && directive == "ifdef")
            return PP_IFDEF;
        else if (directive[0] == 'u' && directive == "undef")
            return PP_UNDEF;
        else if (directive[0] == 'e' && directive == "endif")
            return PP_ENDIF;
        break;

    case 6:
        if (directive[0] == 'i' && directive == "ifndef")
            return PP_IFNDEF;
        else if (directive[0] == 'i' && directive == "import")
            return PP_IMPORT;
        else if (directive[0] == 'd' && directive == "define")
            return PP_DEFINE;
        break;

    case 7:
        if (directive[0] == 'i' && directive == "include")
            return PP_INCLUDE;
        break;

    case 12:
        if (directive[0] == 'i' && directive == "include_next")
            return PP_INCLUDE_NEXT;
        break;

    default:
        break;
    }

    return PP_UNKNOWN_DIRECTIVE;
}

bool Preprocessor::testIfLevel()
{
    const bool result = !_skipping[iflevel++];
    _skipping[iflevel] = _skipping[iflevel - 1];
    _true_test[iflevel] = false;
    return result;
}

int Preprocessor::skipping() const
{ return _skipping[iflevel]; }

Value Preprocessor::evalExpression(TokenIterator firstToken, TokenIterator lastToken,
                                   const QByteArray &source) const
{
    ExpressionEvaluator eval(env);
    const Value result = eval(firstToken, lastToken, source);
    return result;
}

bool Preprocessor::isQtReservedWord(const QByteArray &macroId) const
{
    const int size = macroId.size();
    if      (size == 9 && macroId.at(0) == 'Q' && macroId == "Q_SIGNALS")
        return true;
    else if (size == 7 && macroId.at(0) == 'Q' && macroId == "Q_SLOTS")
        return true;
    else if (size == 8 && macroId.at(0) == 'Q' && macroId == "Q_SIGNAL")
        return true;
    else if (size == 6 && macroId.at(0) == 'Q' && macroId == "Q_SLOT")
        return true;
    else if (size == 6 && macroId.at(0) == 'S' && macroId == "SIGNAL")
        return true;
    else if (size == 4 && macroId.at(0) == 'S' && macroId == "SLOT")
        return true;
    else if (size == 7 && macroId.at(0) == 's' && macroId == "signals")
        return true;
    else if (size == 5 && macroId.at(0) == 's' && macroId == "slots")
        return true;
    return false;
}
