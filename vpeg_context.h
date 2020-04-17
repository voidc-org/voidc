#ifndef VPEG_CONTEXT_H
#define VPEG_CONTEXT_H

#include <utility>
#include <array>
#include <map>

#include <immer/map.hpp>
#include <immer/vector.hpp>

#include "vpeg_parser.h"
#include "vpeg_grammar.h"


//---------------------------------------------------------------------
namespace vpeg
{


//---------------------------------------------------------------------
//- Parsing context
//---------------------------------------------------------------------
class context_t
{
public:
    context_t(std::istream &_input, const grammar_t &_grammar)
      : input(_input),
        grammar(_grammar)
    {
        grammar.check_hash();
    }

public:
    struct variables_t
    {
        immer::map<string, std::any>        values;
        immer::vector<std::array<size_t,2>> strings;
    };

    struct state_t
    {
        size_t      position;
        variables_t variables;
        grammar_t   grammar;
    };

public:
    state_t get_state(void) const
    {
        return {position, variables, grammar};
    }

    void set_state(const state_t &st)
    {
        position  = st.position;
        variables = st.variables;
        grammar   = st.grammar;

        grammar.check_hash();
    }

public:
    bool is_ok(void) { return bool(input); }

public:
    char32_t get_character(void)
    {
        auto c = peek_character();

        position += 1;

        return c;
    }

    char32_t peek_character(void)
    {
        if (position == buffer.size())
        {
            buffer = buffer.push_back(read_character());
        }

        return  buffer[position];
    }

public:
    string take_string(size_t from, size_t to) const
    {
        auto bb = buffer.begin();

        return string(bb+from, bb+to);
    }

public:
    bool expect(char32_t c)
    {
        if (c == peek_character())
        {
            get_character();

            return true;
        }

        return false;
    }

public:
    variables_t variables;
    grammar_t   grammar;        //- ?...

public:     //- ?...
    std::map<std::tuple<size_t, size_t, string>, std::pair<std::any, state_t>> memo;

private:
    std::istream &input;

    size_t position = 0;

    immer::vector<char32_t> buffer;

private:
    char32_t read_character(void);
};


//---------------------------------------------------------------------
}   //- namespace vpeg


#endif      //- VPEG_CONTEXT_H
