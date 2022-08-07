// Copyright (c) 2011-2016 The Bitcoin Core developers
// Copyright (c) 2021-2022 The Paladeum developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PLD_QT_TOKENRECORD_H
#define PLD_QT_TOKENRECORD_H

#include "math.h"
#include "amount.h"
#include "tinyformat.h"


/** UI model for unspent tokens.
 */
class TokenRecord
{
public:

    TokenRecord():
            name(""), quantity(0), units(0), fIsAdministrator(false), fIsLocked(false), ipfshash("")
    {
    }

    TokenRecord(const std::string _name, const CAmount& _quantity, const int _units, const bool _fIsAdministrator, const bool _fIsLocked, const std::string _ipfshash):
            name(_name), quantity(_quantity), units(_units), fIsAdministrator(_fIsAdministrator), fIsLocked(_fIsLocked), ipfshash(_ipfshash)
    {
    }

    std::string formattedQuantity() const {
        bool sign = quantity < 0;
        int64_t n_abs = (sign ? -quantity : quantity);
        int64_t quotient = n_abs / COIN;
        int64_t remainder = n_abs % COIN;
        remainder = remainder / pow(10, 8 - units);

        if (remainder == 0) {
            return strprintf("%s%d", sign ? "-" : "", quotient);
        }
        else {
            return strprintf("%s%d.%0" + std::to_string(units) + "d", sign ? "-" : "", quotient, remainder);
        }
    }

    /** @name Immutable attributes
      @{*/
    std::string name;
    CAmount quantity;
    int units;
    bool fIsAdministrator;
    bool fIsLocked;
    std::string ipfshash;
    /**@}*/

};

#endif // PLD_QT_TOKENRECORD_H
