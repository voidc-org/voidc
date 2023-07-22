//---------------------------------------------------------------------
//- Copyright (C) 2020-2023 Dmitry Borodkin <borodkin.dn@gmail.com>
//- SDPX-License-Identifier: LGPL-3.0-or-later
//---------------------------------------------------------------------
#ifndef VOIDC_TARGET_H
#define VOIDC_TARGET_H

#include "voidc_ast.h"
#include "voidc_types.h"
#include "voidc_util.h"
#include "voidc_visitor.h"

#include <string>
#include <vector>
#include <set>
#include <map>
#include <forward_list>
#include <deque>
#include <utility>

#include <immer/map.hpp>

#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>

#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/IR/IRBuilder.h>


//---------------------------------------------------------------------
class base_local_ctx_t;

extern "C"
{
    typedef void (*compile_ctx_cleaner_t)(void *data);

    typedef void (*adopt_result_t)(void *ctx, v_type_t *type, LLVMValueRef value);

    typedef LLVMValueRef (*convert_to_type_t)(void *ctx, v_type_t *t0, LLVMValueRef v0, v_type_t *t1);

    typedef LLVMValueRef (*make_temporary_t)(void *ctx, v_type_t *t, LLVMValueRef v);

    typedef compile_ctx_cleaner_t temporary_cleaner_t;

    typedef LLVMModuleRef (*obtain_module_t)(void *ctx);

    typedef void (*finish_module_t)(void *ctx, LLVMModuleRef module);
}


//---------------------------------------------------------------------
//- Base Compilation Context
//---------------------------------------------------------------------
class base_compile_ctx_t
{
public:
    base_compile_ctx_t() = default;
    virtual ~base_compile_ctx_t() = default;

public:
    using intrinsic_t = std::pair<void *, void *>;          //- I.e. (function, context)

    struct declarations_t
    {
        immer::map<v_quark_t, v_quark_t>   aliases;
        immer::map<v_quark_t, v_type_t *>  constants;
        immer::map<v_quark_t, v_type_t *>  symbols;
        immer::map<v_quark_t, intrinsic_t> intrinsics;

        bool empty(void) const
        {
            return (aliases.empty() && constants.empty() && symbols.empty() && intrinsics.empty());
        }

        void aliases_insert   (std::pair<v_quark_t, v_quark_t>   v) { aliases    = aliases.insert(v); }
        void constants_insert (std::pair<v_quark_t, v_type_t *>  v) { constants  = constants.insert(v); }
        void symbols_insert   (std::pair<v_quark_t, v_type_t *>  v) { symbols    = symbols.insert(v); }
        void intrinsics_insert(std::pair<v_quark_t, intrinsic_t> v) { intrinsics = intrinsics.insert(v); }

        void insert(const declarations_t &other);
    };

    declarations_t decls;

    using typenames_t = immer::map<v_type_t *, v_quark_t>;

public:
    std::map<v_quark_t, LLVMValueRef> constant_values;

public:
    virtual void add_symbol_value(v_quark_t raw_name, void *value) = 0;

public:
    v_type_t *get_symbol_type(v_quark_t raw_name) const
    {
        auto *pt = decls.symbols.find(raw_name);

        if (pt) return *pt;
        else    return nullptr;
    }

public:
    using cleaners_t = std::forward_list<std::pair<compile_ctx_cleaner_t, void *>>;

    void add_cleaner(compile_ctx_cleaner_t fun, void *data)
    {
        cleaners.push_front({fun, data});
    }

protected:
    void run_cleaners(void)
    {
        for (auto &it: cleaners) it.first(it.second);

        cleaners.clear();       //- Sic!
    }

private:
    cleaners_t cleaners;
};


//---------------------------------------------------------------------
//- Base Global Context
//---------------------------------------------------------------------
class base_global_ctx_t : public base_compile_ctx_t, public voidc_types_ctx_t
{
public:
    base_global_ctx_t(LLVMContextRef ctx, size_t int_size, size_t long_size, size_t ptr_size);
    ~base_global_ctx_t() override;

public:
    std::map<std::string, declarations_t> imported;

public:
    LLVMBuilderRef builder;

    LLVMTargetDataRef data_layout = nullptr;

public:
    virtual void initialize_type(v_quark_t raw_name, v_type_t *type);

    v_type_t * const char_ptr_type;
    v_type_t * const void_ptr_type;

public:
    static void verify_module(LLVMModuleRef module);

public:
    base_local_ctx_t *local_ctx = nullptr;

protected:
    void initialize(void);
};


//---------------------------------------------------------------------
//- Base Local Context
//---------------------------------------------------------------------
class base_local_ctx_t : public base_compile_ctx_t
{
public:
    explicit base_local_ctx_t(base_global_ctx_t &global);
    ~base_local_ctx_t() override;

public:
    std::string filename;

    base_global_ctx_t &global_ctx;

public:
    visitor_t compiler;

public:
    std::set<std::string> imported;
    std::set<std::string> exported;

public:
    declarations_t *export_decls = nullptr;

    void export_alias(v_quark_t name, v_quark_t raw_name);
    void add_alias(v_quark_t name, v_quark_t raw_name);

    void export_constant(v_quark_t raw_name, v_type_t *type, LLVMValueRef value);
    void add_constant(v_quark_t raw_name, v_type_t *type, LLVMValueRef value);

    void export_symbol(v_quark_t raw_name, v_type_t *type, void *value);
    void add_symbol(v_quark_t raw_name, v_type_t *type, void *value);

    void export_intrinsic(v_quark_t fun_name, void *fun, void *aux=nullptr);
    void add_intrinsic(v_quark_t fun_name, void *fun, void *aux=nullptr);

    virtual void export_type(v_quark_t raw_name, v_type_t *type);
    virtual void add_type(v_quark_t raw_name, v_type_t *type);

public:
    v_quark_t check_alias(v_quark_t name);

public:
    v_type_t *find_type(v_quark_t type_name);                       //- Alias checked


    bool find_constant(v_quark_t raw_name, v_type_t * &type, LLVMValueRef &value);

    bool find_symbol(v_quark_t raw_name, v_type_t * &type, void * &value);      //- ???


    virtual void *find_symbol_value(v_quark_t raw_name) = 0;        //- No alias check!

public:
    LLVMModuleRef module = nullptr;

    obtain_module_t obtain_module_fun = nullptr;
    void           *obtain_module_ctx = nullptr;

    LLVMModuleRef obtain_module(void)
    {
        if (obtain_module_fun)  return  obtain_module_fun(obtain_module_ctx);
        else                    return  nullptr;        //- Sic!
    }

    bool obtain_identifier(v_quark_t name, v_type_t * &type, LLVMValueRef &value);

    finish_module_t finish_module_fun = nullptr;
    void           *finish_module_ctx = nullptr;

    void finish_module(LLVMModuleRef mod)
    {
        if (finish_module_fun)  finish_module_fun(finish_module_ctx, mod);
    }

public:
    void push_builder_ip(void);
    void pop_builder_ip(void);

private:
    std::forward_list<llvm::IRBuilderBase::InsertPoint> builder_ip_stack;

public:
    LLVMValueRef prepare_function(const char *name, v_type_t *type);
    void finish_function(void);

    LLVMBasicBlockRef function_leave_b = nullptr;

public:
    typedef immer::map<v_quark_t, std::pair<v_type_t *, LLVMValueRef>> variables_t;

    variables_t vars;

public:
    void push_variables(void);
    void pop_variables(void);

private:
    std::forward_list<std::pair<declarations_t, variables_t>> vars_stack;       //- Sic!

public:
    v_type_t    *result_type  = nullptr;
    LLVMValueRef result_value = nullptr;

    adopt_result_t adopt_result_fun;
    void          *adopt_result_ctx;

    void adopt_result(v_type_t *type, LLVMValueRef value)
    {
        return adopt_result_fun(adopt_result_ctx, type, value);
    }

public:
    convert_to_type_t convert_to_type_fun;
    void             *convert_to_type_ctx;

    LLVMValueRef convert_to_type(v_type_t *t0, LLVMValueRef v0, v_type_t *t1)
    {
        return convert_to_type_fun(convert_to_type_ctx, t0, v0, t1);
    }

public:
    make_temporary_t make_temporary_fun;
    void            *make_temporary_ctx;

    LLVMValueRef make_temporary(v_type_t *t, LLVMValueRef v)
    {
        return make_temporary_fun(make_temporary_ctx, t, v);
    }

    void add_temporary_cleaner(temporary_cleaner_t fun, void *data);

    void push_temporaries(void);
    void pop_temporaries(void);

public:
    std::forward_list<std::pair<LLVMValueRef, cleaners_t>> temporaries_stack;

public:
    bool has_parent(void) const { return bool(parent_ctx); }

private:
    friend class voidc_local_ctx_t;

    base_local_ctx_t * const parent_ctx = nullptr;
};


//---------------------------------------------------------------------
//- Voidc template Context
//---------------------------------------------------------------------
template<typename T, typename... Targs>
class voidc_template_ctx_t: public T
{
public:
    explicit voidc_template_ctx_t(Targs... args)
      : T(args...)
    {}
    ~voidc_template_ctx_t() override;

public:
    LLVMOrcJITDylibRef base_jd = nullptr;

public:
    void add_symbol_value(v_quark_t raw_name, void *value) override;

public:
    llvm::orc::SymbolMap unit_symbols;

    void flush_unit_symbols(void);

public:
    void add_object_file_to_jit(LLVMMemoryBufferRef membuf);

    void add_module_to_jit(LLVMModuleRef module);

public:
    std::deque<LLVMOrcJITDylibRef> deque_jd;

public:
    void setup_link_order(LLVMOrcJITDylibRef jd);
};

using voidc_global_template_ctx_t = voidc_template_ctx_t<base_global_ctx_t, LLVMContextRef, size_t, size_t, size_t>;
using voidc_local_template_ctx_t  = voidc_template_ctx_t<base_local_ctx_t, base_global_ctx_t &>;

template<> void voidc_global_template_ctx_t::flush_unit_symbols(void);
template<> void voidc_local_template_ctx_t::flush_unit_symbols(void);

template<> void voidc_global_template_ctx_t::setup_link_order(LLVMOrcJITDylibRef jd);
template<> void voidc_local_template_ctx_t::setup_link_order(LLVMOrcJITDylibRef jd);

extern template class voidc_template_ctx_t<base_global_ctx_t, LLVMContextRef, size_t, size_t, size_t>;
extern template class voidc_template_ctx_t<base_local_ctx_t, base_global_ctx_t &>;


//---------------------------------------------------------------------
//- Voidc Global Context
//---------------------------------------------------------------------
class voidc_global_ctx_t : public voidc_global_template_ctx_t
{
public:
    voidc_global_ctx_t();
    ~voidc_global_ctx_t() = default;

public:
    void initialize_type(v_quark_t raw_name, v_type_t *type) override;

    typenames_t typenames;

    std::map<std::string, typenames_t> imported_typenames;

public:
    static void static_initialize(void);
    static void static_terminate(void);

public:
    VOIDC_DLLEXPORT static voidc_global_ctx_t * const & voidc;
    VOIDC_DLLEXPORT static base_global_ctx_t  *         target;

public:
    VOIDC_DLLEXPORT static LLVMOrcLLJITRef jit;

    VOIDC_DLLEXPORT static LLVMTargetMachineRef target_machine;
    VOIDC_DLLEXPORT static LLVMPassManagerRef   pass_manager;

public:
    static void prepare_module_for_jit(LLVMModuleRef module);

public:
    v_type_t * const static_type_type;
    v_type_t * const type_type;

    v_type_t * const type_ptr_type;

public:
    int jd_hash = 0;
};

//---------------------------------------------------------------------
//- Voidc Local Context
//---------------------------------------------------------------------
class voidc_local_ctx_t : public voidc_local_template_ctx_t
{
public:
    explicit voidc_local_ctx_t(voidc_global_ctx_t &global);
    ~voidc_local_ctx_t() override;

public:
    std::set<std::string> imports;

public:
    typenames_t *export_typenames = nullptr;
    typenames_t  typenames;

    void export_type(v_quark_t raw_name, v_type_t *type) override;
    void add_type(v_quark_t raw_name, v_type_t *type) override;

public:
    void *find_symbol_value(v_quark_t raw_name) override;           //- No check alias!

public:
    void prepare_unit_action(int line, int column);
    void finish_unit_action(void);
    void run_unit_action(void);

    LLVMMemoryBufferRef unit_buffer = nullptr;
};


//---------------------------------------------------------------------
//- Target template Context
//---------------------------------------------------------------------
template<typename T, typename... Targs>
class target_template_ctx_t: public T
{
public:
    explicit target_template_ctx_t(Targs... args)
      : T(args...)
    {}

public:
    void add_symbol_value(v_quark_t raw_name, void *value) override;

protected:
    std::map<v_quark_t, void *> symbol_values;
};

extern template class target_template_ctx_t<base_global_ctx_t, LLVMContextRef, size_t, size_t, size_t>;
extern template class target_template_ctx_t<base_local_ctx_t, base_global_ctx_t &>;


//---------------------------------------------------------------------
//- Target Global Context
//---------------------------------------------------------------------
class target_global_ctx_t : public target_template_ctx_t<base_global_ctx_t, LLVMContextRef, size_t, size_t, size_t>
{
public:
    target_global_ctx_t(size_t int_size, size_t long_size, size_t ptr_size);
    ~target_global_ctx_t() override;

private:
    friend class target_local_ctx_t;
};

//---------------------------------------------------------------------
//- Target Local Context
//---------------------------------------------------------------------
class target_local_ctx_t : public target_template_ctx_t<base_local_ctx_t, base_global_ctx_t &>
{
public:
    explicit target_local_ctx_t(base_global_ctx_t &global);
    ~target_local_ctx_t() override;

public:
    void *find_symbol_value(v_quark_t raw_name) override;           //- No check alias!
};


#endif  //- VOIDC_TARGET_H
