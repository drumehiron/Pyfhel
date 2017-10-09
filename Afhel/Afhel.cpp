/*
 * Afhel
 * --------------------------------------------------------------------
 *  Afhel is a C++ library that creates an abstraction over the basic
 *  functionalities of HElib as a Homomorphic Encryption library, such as
 *  addition, multiplication, scalar product and others.
 *
 *  Afhel implements a higher level of abstraction than HElib, and handles
 *  Cyphertexts using an unordered map (key-value pairs) that is accessed
 *  via keys of type string. This is done in order to manage Cyphertext 
 *  using references (the keys), which will allow Pyfhel to work only 
 *  using strings (keeping the Cyphertexts in C++). Afhel also compresses
 *  the Context setup and Key generation into one single KeyGen function
 *  with multiple parameter selection.
 *  --------------------------------------------------------------------
 *  Author: Alberto Ibarrondo
 *  Date: 14/06/2017  
 *  --------------------------------------------------------------------
 *  License: GNU GPL v3
 *  
 *  Afhel is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Pyfhel is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *  --------------------------------------------------------------------
 */


#include <NTL/ZZ.h>
#include <NTL/ZZX.h>
//#include <NTL/Vector.h>
#include <NTL/BasicThreadPool.h>
#include <NTL/lzz_pXFactoring.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <unistd.h>


#include "FHE.h"
#include "timing.h"
#include "EncryptedArray.h"
#include "Afhel.h"

using namespace std;

Afhel::Afhel(){}
Afhel::~Afhel(){}

// ------------------------------ CRYPTOGRAPHY --------------------------------
// KEY GENERATION
void Afhel::keyGen(long p, long r, long c, long w, long d, long sec,
                       long L, long m, long R, long s,
                       const vector<long>& gens,
                       const vector<long>& ords){
        if(flagPrint){std::cout << "Afhel::keyGen START" << endl;}
        
        // Initializing possible empty parameters for context
        //  - L -> Heuristic computation
        if(L==-1){
            L=3*R+3;
            if(p>2 || r>1){
                 L += R * 2*ceil(log((double)p)*r*3)/(log(2.0)*FHE_p2Size) +1;
            }
            if(flagPrint){std::cout << "  - calculated L: " << L <<endl;}
        }
        //  - m -> use HElib method FindM with other parameters
        if(m==-1){
            m = FindM(sec, L, c, p, d, s, 0, 0);
            if(flagPrint){std::cout << "  - Calculated m: " << m <<endl;}
        }

        // Context creation
        context = new FHEcontext(m, p, r, gens, ords);  // Initialize context
        buildModChain(*context, L, c);                  // Add primes to modulus chain
        if(flagPrint){std::cout << "  - Created Context: " 
            << "p="   << p        << ", r=" << r
            << ", d=" << d        << ", c=" << c
            << ", sec=" << sec    << ", w=" << w
            << ", L=" << L        << ", m=" << m
            << ", gens=" << gens  << ", ords=" << ords <<  endl;}

        // ZZX Polynomial creation
        ZZX G;
        if (d == 0){  G = context->alMod.getFactorsOverZZ()[0];}
        else       {  G = makeIrredPoly(p, d);}
        if(flagPrint){std::cout << "  - Created ZZX poly from NTL lib" <<endl;}

        // Secret/Public key pair creation
        secretKey = new FHESecKey(*context);            // Initialize object
        publicKey = (FHEPubKey*) secretKey;             // Upcast: FHESecKey to FHEPubKey
        secretKey->GenSecKey(w);                        // Hamming-weight-w secret key
        if(flagPrint){std::cout << "  - Created Public/Private Key Pair" << endl;} 

        // Additional initializations
        addSome1DMatrices(*secretKey);                  // Key-switch matrices for relin.
        ea = new EncryptedArray(*context, G);           // Object for packing in subfields
        nslots = ea->size();


        if(flagPrint){std::cout << "Afhel::keyGen COMPLETED" << endl;}
}

// ENCRYPTION
string Afhel::encrypt(vector<long> plaintext) {
        Ctxt cyphertext(*publicKey);                    // Empty cyphertext object
        //TODO: create a vector of size nddSlots and fill it first with values from plaintext, then with zeros
        ea->encrypt(cyphertext, *publicKey, plaintext); // Encrypt plaintext
        string id1 = store(&cyphertext);
        if(flagPrint){
            std::cout << "  Afhel::encrypt({ID" << id1 << "}[" << plaintext <<  "])" << endl;
        }
        return id1;
}

// DECRYPTION
vector<long> Afhel::decrypt(string id1) {
        vector<long> res(nslots, 0);                    // Empty vector of values
        ea->decrypt(ctxtMap.at(id1), *secretKey, res);  // Decrypt cyphertext
        if(flagPrint){
            std::cout << "  Afhel::decrypt({ID" << id1 << "}[" << res << "])" << endl;
        }
        return res;
}


// ---------------------------- OPERATIONS ------------------------------------
// ADDITION
void Afhel::add(string id1, string id2, bool negative){
        ctxtMap.at(id1).addCtxt(ctxtMap.at(id2), negative);
}

// MULTIPLICATION
void Afhel::mult(string id1, string id2){
        ctxtMap.at(id1).multiplyBy(ctxtMap.at(id2));
}

// MULTIPLICATION BY 2
void Afhel::mult3(string id1, string id2, string id3){
        ctxtMap.at(id1).multiplyBy2(ctxtMap.at(id2), ctxtMap.at(id3));
}

// SCALAR PRODUCT
void Afhel::scalarProd(string id1, string id2, int partitionSize){
        ctxtMap.at(id1).multiplyBy(ctxtMap.at(id2));
        totalSums(*ea, ctxtMap.at(id1));
}

// SQUARE
void Afhel::square(string id1){
        ctxtMap.at(id1).square();
}

// CUBE
void Afhel::cube(string id1){
        ctxtMap.at(id1).cube();
}

// NEGATE
void Afhel::negate(string id1){
        ctxtMap.at(id1).negate();
}

// COMPARE EQUALS
bool Afhel::equalsTo(string id1, string id2, bool comparePkeys){
        return ctxtMap.at(id1).equalsTo(ctxtMap.at(id2), comparePkeys);
}

// ROTATE
void Afhel::rotate(string id1, long c){
        ea->rotate(ctxtMap.at(id1), c);
}

// SHIFT
void Afhel::shift(string id1, long c){
        ea->shift(ctxtMap.at(id1), c);
}


// ------------------------------------- I/O ----------------------------------
// SAVE ENVIRONMENT
bool Afhel::saveEnv(string fileName){
    bool res=1;
    try{
        fstream keyFile(fileName, fstream::out|fstream::trunc);
        assert(keyFile.is_open());
        writeContextBase(keyFile, *context);
        keyFile << *context << endl;       
        keyFile << *secretKey << endl;
        keyFile.close();
    }
    catch(exception& e){
        res=0;
    }
    return res;
}

// RESTORE ENVIRONMENT
bool Afhel::restoreEnv(string fileName){
    bool res=1;
    try{
        fstream keyFile(fileName, fstream::in);
        unsigned long m1, p1, r1;
        vector<long> gens, ords;
        readContextBase(keyFile, m1, p1, r1, gens, ords);
        FHEcontext tmpContext(m1, p1, r1, gens, ords);
        FHESecKey tmpSecretKey(tmpContext);
        keyFile >> tmpContext;
        keyFile >> tmpSecretKey;
        context = &tmpContext;
        secretKey = &tmpSecretKey;
        ZZX G = context->alMod.getFactorsOverZZ()[0];
        ea = new EncryptedArray(*context, G);
        publicKey = (FHEPubKey*) secretKey;   // Upcast: FHESecKey to FHEPubKey
        nslots = ea->size();                  // Refill nslots
    }
    catch(exception& e){
        res=0;
    }
    return res;
}


// --------------------------------- AUXILIARY --------------------------------

long Afhel::numSlots() {
    return ea->size();
}

string Afhel::store(Ctxt* ctxt) {
    struct timeval tp;
    gettimeofday(&tp, NULL);
    long int ms = tp.tv_sec * 1000 + tp.tv_usec / 1000;
    string id1 = boost::lexical_cast<string>(ms);
    ctxtMap.insert(make_pair(id1, *ctxt));
    return id1;
}

string Afhel::set(string id1){
    Ctxt ctxt = ctxtMap.at(id1);
    return store(&ctxt);
}

Ctxt Afhel::retrieve(string id1) {
    return ctxtMap.at(id1);
}

void Afhel::replace(string id1, Ctxt new_ctxt) {
    boost::unordered_map<string, Ctxt>::const_iterator i = ctxtMap.find(id1);
    if(i != ctxtMap.end()) {
        ctxtMap.at(id1) = new_ctxt;
    }
}

void Afhel::erase(string id1) {
    if(ctxtMap.find(id1) != ctxtMap.end()) {
        ctxtMap.erase(id1);
    }
}
