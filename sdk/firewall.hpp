#pragma once

#include <eosiolib/eosio.hpp>
#include <eosiolib/transaction.hpp>
#include <eosiolib/crypto.h>
#include <eosiolib/asset.hpp>
#include <eosiolib/singleton.hpp>
#include <eosiolib/currency.hpp>
#include <eosiolib/vector.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/split.hpp>

#ifndef FIREWALL_CONTRACT
    #define FIREWALL_CONTRACT            N(firewall.x)
#endif

#define FIREWALL_STATUS_NORMAL       10000 // 正常
#define FIREWALL_STATUS_WHITE        10001 // 调用者是在白名单内
#define FIREWALL_STATUS_CONTRACT     10002 // 调用者是智能合约
#define FIREWALL_STATUS_BLACK        10003 // 调用者是在黑名单内
#define FIREWALL_STATUS_MALICIOUS    10004 // 调用者是在恶意地址库内
#define FIREWALL_STATUS_SUSPECT      10005 // 调用者是在可疑地址库内
#define FIREWALL_STATUS_DANGER       99999 // 有风险

namespace eosio {
    class firewall : public contract {
    public:
        firewall( account_name self ):
            contract(self),
            _global(_self, _self){};
        
        // 是否有风险
        inline uint32_t check_user( account_name user );

        // 是否维护状态
        inline uint32_t check(); 

        // 是否为合约账号
        inline bool is_contract( account_name user )const;

        // 是否为恶意地址
        inline bool is_malicious( account_name user )const;

        // 是否白名单
        inline bool is_white( account_name user )const;

        // 是否黑名单
        inline bool is_black( account_name user )const;

        // 是否是系统账号
        inline bool is_system( account_name user )const;

        // 是否是可疑账号
        inline bool is_suspect( account_name user )const;

        // 扩展库
        inline bool in_extends( account_name user, string category)const;

    private:
        struct black_lst {
            account_name account;

            account_name primary_key() const { return account; }

            EOSLIB_SERIALIZE( black_lst, (account) )
        };

        struct white_lst {
            account_name account;

            account_name primary_key() const { return account; }

            EOSLIB_SERIALIZE( white_lst, (account) )
        };

        struct malicious_lst {
            uint64_t          id;
            checksum256       acnthash;
            
            uint64_t primary_key()const { return id; }

            key256 by_acnthash()const { return get_acnthash(acnthash); }

            static key256 get_acnthash(const checksum256& acnthash) {
                const uint64_t *p64 = reinterpret_cast<const uint64_t *>(&acnthash);
                return key256::make_from_word_sequence<uint64_t>(p64[0], p64[1], p64[2], p64[3]);
            }

            EOSLIB_SERIALIZE( malicious_lst, (id)(acnthash) )
        };

        struct contract_lst {
            account_name account;

            account_name primary_key() const { return account; }

            EOSLIB_SERIALIZE( contract_lst, (account) )
        };
        
        struct member_lst {
            account_name dapp;
            account_name manage;
            extended_asset donate;
            bool maintain;
            bool bti;
            bool contract;
            bool suspect;
            eosio::string extends;
            uint64_t create_at;

            account_name primary_key() const { return dapp; }

            EOSLIB_SERIALIZE( member_lst, (dapp)(manage)(donate)(maintain)(bti)(contract)(suspect)(extends)(create_at) )
        };

        struct extends_lst {
            uint64_t          id;
            checksum256       digest;
            
            uint64_t primary_key()const { return id; }

            key256 by_digest()const { return get_digest(digest); }

            static key256 get_digest(const checksum256& digest) {
                const uint64_t *p64 = reinterpret_cast<const uint64_t *>(&digest);
                return key256::make_from_word_sequence<uint64_t>(p64[0], p64[1], p64[2], p64[3]);
            }

            EOSLIB_SERIALIZE( extends_lst, (id)(digest) )
        };

        struct st_global
        {
            uint64_t current_id;
        };

        typedef multi_index<N(blacklst), black_lst> blacklst_index;

        typedef multi_index<N(whitelst), white_lst> whitelst_index;

        typedef multi_index< N(malicious), malicious_lst,
                indexed_by< N(acnthash), const_mem_fun<malicious_lst, key256,  &malicious_lst::by_acnthash> >
            > malicious_index;

        typedef multi_index<N(contractlst), contract_lst> contractlst_index;

        typedef multi_index<N(member), member_lst> member_index;

        typedef multi_index< N(extends), extends_lst,
                indexed_by< N(digest), const_mem_fun<extends_lst, key256,  &extends_lst::by_digest> >
            > extends_index;

        typedef singleton<N(global), st_global> tb_global;
        tb_global _global;

        uint64_t next_id()
        {
            st_global global = _global.get_or_default();
            global.current_id += 1;
            _global.set(global, _self);
            return global.current_id;
        }
        
        void set_log()
        {
            auto size = transaction_size();
            char buf[size];
            uint32_t read = read_transaction( buf, size );
            if(size == read){
                checksum256 h;
                sha256(buf, read, &h);
                transaction tx;
                tx.actions.emplace_back( permission_level{ _self, N(active) }, FIREWALL_CONTRACT, N(trxlog), std::make_tuple(h));
                tx.delay_sec = 0;
                tx.send(next_id(), _self);
            }
        }

        void set_stat()
        {
            transaction tx;
            tx.actions.emplace_back( permission_level{ _self, N(active) }, FIREWALL_CONTRACT, N(stat), std::make_tuple());
            tx.delay_sec = 0;
            tx.send(next_id(), _self);
        }
    };

    uint32_t firewall::check()
    {
        member_index _table(FIREWALL_CONTRACT, FIREWALL_CONTRACT);
        auto _config = _table.find(_self);
        eosio_assert(_config != _table.end(), "DApp not exist");
        eosio_assert(_config->maintain==false, "sorry, DApp is under maintenance");
        set_stat();
        auto size = transaction_size();
        char buf[size];
        uint32_t read = read_transaction( buf, size );
        auto trx = unpack<transaction>( buf, sizeof(buf) );
        auto actor = trx.actions.front().authorization.front().actor;

        auto status = check_user(actor);
        if(_config->bti && status == FIREWALL_STATUS_MALICIOUS){
            return FIREWALL_STATUS_DANGER;
        }else if(_config->contract && status == FIREWALL_STATUS_CONTRACT){
            return FIREWALL_STATUS_DANGER;
        }else if(_config->suspect && status == FIREWALL_STATUS_SUSPECT){
            return FIREWALL_STATUS_DANGER;
        }else if(!_config->extends.empty()){
            vector<string> strs;
            boost::split(strs, _config->extends, boost::is_any_of(","));
            for (auto category : strs)
            {
                if(in_extends(actor, category))
                    return FIREWALL_STATUS_DANGER;
            }
        }else if(status == FIREWALL_STATUS_BLACK){
            return FIREWALL_STATUS_DANGER;
        }
        return status;
    }

    uint32_t firewall::check_user( account_name user )
    {
        if(is_white(user)){
            return FIREWALL_STATUS_WHITE;
        }
        if(is_system(user)){
            return FIREWALL_STATUS_WHITE;
        }
        if(is_contract(user)){
            set_log();
            return FIREWALL_STATUS_CONTRACT;
        }
        if(is_black(user)){
            set_log();
            return FIREWALL_STATUS_BLACK;
        }
        if(is_suspect(user)){
            set_log();
            return FIREWALL_STATUS_SUSPECT;
        }
        if(is_malicious(user)){
            set_log();
            return FIREWALL_STATUS_MALICIOUS;
        }
        return FIREWALL_STATUS_NORMAL;
    }

    bool firewall::is_contract( account_name user )const
    {
        contractlst_index _table(FIREWALL_CONTRACT, FIREWALL_CONTRACT);
        auto iter = _table.find(user);
        if (iter != _table.end()) {
            return true;
        }else{
            return false;
        }
    }

    bool firewall::is_malicious( account_name user )const
    {
        auto n = name{user};
        string str = n.to_string();
		checksum256 acnthash;
		sha256((char *)&str, sizeof(str), &acnthash);
        malicious_index _table(FIREWALL_CONTRACT, FIREWALL_CONTRACT);
        auto idx = _table.template get_index<N(acnthash)>();
        auto iter = idx.find( malicious_lst::get_acnthash(acnthash) );
        if (iter == idx.end()) {
            return true;
        }else{
            return false;
        }
    }

    bool firewall::is_white( account_name user )const
    {
        whitelst_index _table(FIREWALL_CONTRACT, _self);
        auto iter = _table.find(user);
        if (iter != _table.end()) {
            return true;
        }else{
            return false;
        }
    }

    bool firewall::is_system( account_name user )const
    {
        whitelst_index _table(FIREWALL_CONTRACT, FIREWALL_CONTRACT);
        auto iter = _table.find(user);
        if (iter != _table.end()) {
            return true;
        }else{
            return false;
        }
    }

    bool firewall::is_black( account_name user )const
    {
        blacklst_index _table(FIREWALL_CONTRACT, _self);
        auto iter = _table.find(user);
        if (iter != _table.end()) {
            return true;
        }else{
            return false;
        }
    }

    bool firewall::is_suspect( account_name user )const
    {
        blacklst_index _table(FIREWALL_CONTRACT, FIREWALL_CONTRACT);
        auto iter = _table.find(user);
        if (iter != _table.end()) {
            return true;
        }else{
            return false;
        }
    }

    bool firewall::in_extends(account_name user, string category)const
    {
        extends_index _table(FIREWALL_CONTRACT, FIREWALL_CONTRACT);
        auto idx = _table.template get_index<N(digest)>();
        auto seed = name{user}.to_string() + category;
        checksum256 digest;
        sha256( (char *)&seed, sizeof(seed), &digest);
        auto iter = idx.find( extends_lst::get_digest(digest) );
        if (iter != idx.end()) {
            return true;
        }
        return false;
    }
} /// namespace eosio