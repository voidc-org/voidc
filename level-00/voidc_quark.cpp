//---------------------------------------------------------------------
//- Copyright (C) 2020-2021 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#include "voidc_quark.h"

#include <cstdio>
#include <cassert>

#include <unordered_map>

#include <immer/vector_transient.hpp>


//---------------------------------------------------------------------
//- Globals
//---------------------------------------------------------------------
static std::unordered_map<std::string, v_quark_t> voidc_quark_from_string;

static immer::vector_transient<const char *> voidc_quark_to_string;


//---------------------------------------------------------------------
//- Basics
//---------------------------------------------------------------------
const v_quark_t *
v_quark_ptr_from_string(const char *str)
{
    if (str == nullptr)
    {
        static const v_quark_t zero = 0;

        return &zero;
    }

    assert(voidc_quark_to_string.size() == voidc_quark_from_string.size());

    auto q = v_quark_t(voidc_quark_from_string.size() + 1);     //- Sic!

    auto [it, ok] = voidc_quark_from_string.insert({str, q});

    if (ok) voidc_quark_to_string.push_back( it->first.c_str() );

    return &it->second;
}

//---------------------------------------------------------------------
v_quark_t
v_quark_from_string(const char *str)
{
    return *v_quark_ptr_from_string(str);
}


//---------------------------------------------------------------------
const char *
v_quark_to_string(v_quark_t vq)
{
    if (vq == 0)  return nullptr;

    return  voidc_quark_to_string[vq-1];        //- Sic!
}


//---------------------------------------------------------------------
//- Utility
//---------------------------------------------------------------------
v_quark_t
v_quark_try_string(const char *str)
{
    if (str == nullptr) return 0;

    auto it = voidc_quark_from_string.find(str);

    if (it == voidc_quark_from_string.end())  return 0;

    return it->second;
}

//---------------------------------------------------------------------
const char *
v_intern_string(const char *str)
{
    if (str == nullptr) return nullptr;

    assert(voidc_quark_to_string.size() == voidc_quark_from_string.size());

    auto q = v_quark_t(voidc_quark_from_string.size() + 1);     //- Sic!

    auto [it, ok] = voidc_quark_from_string.insert({str, q});

    auto s = it->first.c_str();

    if (ok) voidc_quark_to_string.push_back(s);

    return s;
}

