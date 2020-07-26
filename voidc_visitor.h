//---------------------------------------------------------------------
//- Copyright (C) 2020 Dmitry Borodkin <borodkin-dn@yandex.ru>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_VISITOR_H
#define VOIDC_VISITOR_H

#include "voidc_quark.h"
#include "voidc_dllexport.h"

#include <immer/map.hpp>


//---------------------------------------------------------------------
//- ...
//---------------------------------------------------------------------
class voidc_visitor_t
{
public:
    using void_methods_map_t = immer::map<v_quark_t, void *>;

public:
    voidc_visitor_t()  = default;
    ~voidc_visitor_t() = default;

public:
    voidc_visitor_t(const voidc_visitor_t &vis)
      : _void_methods(vis.void_methods)
    {}

    voidc_visitor_t &operator=(const voidc_visitor_t &vis)
    {
        _void_methods = vis.void_methods;

        return *this;
    }

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    voidc_visitor_t set_void_method(v_quark_t q, void *void_method) const
    {
        return  voidc_visitor_t(void_methods.set(q, void_method));
    }

public:
    const void_methods_map_t &void_methods = _void_methods;

private:
    void_methods_map_t _void_methods;

private:
    explicit voidc_visitor_t(const void_methods_map_t &vm)
      : _void_methods(vm)
    {}
};

typedef std::shared_ptr<const voidc_visitor_t> visitor_ptr_t;

//---------------------------------------------------------------------
extern "C"
{

VOIDC_DLLEXPORT_BEGIN_VARIABLE

extern visitor_ptr_t voidc_visitor;

VOIDC_DLLEXPORT_END

//---------------------------------------------------------------------
}   //- extern "C"


#endif  //- VOIDC_VISITOR_H
