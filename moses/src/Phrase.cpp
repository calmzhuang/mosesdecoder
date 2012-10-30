// $Id$
// vim:tabstop=2

/***********************************************************************
Moses - factored phrase-based language decoder
Copyright (C) 2006 University of Edinburgh

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
***********************************************************************/

#include "util/check.hh"
#include <algorithm>
#include <sstream>
#include <string>
#include "memory.h"
#include "FactorCollection.h"
#include "Phrase.h"
#include "StaticData.h"  // GetMaxNumFactors

#include "util/string_piece.hh"
#include "util/tokenize_piece.hh"

using namespace std;

namespace Moses
{

Phrase::Phrase() {}

Phrase::Phrase(size_t reserveSize)
{
  m_words.reserve(reserveSize);
}

Phrase::Phrase(const vector< const Word* > &mergeWords)
{
  m_words.reserve(mergeWords.size());
  for (size_t currPos = 0 ; currPos < mergeWords.size() ; currPos++) {
    AddWord(*mergeWords[currPos]);
  }
}

Phrase::~Phrase()
{
}

void Phrase::MergeFactors(const Phrase &copy)
{
  CHECK(GetSize() == copy.GetSize());
  size_t size = GetSize();
  const size_t maxNumFactors = MAX_NUM_FACTORS;
  for (size_t currPos = 0 ; currPos < size ; currPos++) {
    for (unsigned int currFactor = 0 ; currFactor < maxNumFactors ; currFactor++) {
      FactorType factorType = static_cast<FactorType>(currFactor);
      const Factor *factor = copy.GetFactor(currPos, factorType);
      if (factor != NULL)
        SetFactor(currPos, factorType, factor);
    }
  }
}

void Phrase::MergeFactors(const Phrase &copy, FactorType factorType)
{
  CHECK(GetSize() == copy.GetSize());
  for (size_t currPos = 0 ; currPos < GetSize() ; currPos++)
    SetFactor(currPos, factorType, copy.GetFactor(currPos, factorType));
}

void Phrase::MergeFactors(const Phrase &copy, const std::vector<FactorType>& factorVec)
{
  CHECK(GetSize() == copy.GetSize());
  for (size_t currPos = 0 ; currPos < GetSize() ; currPos++)
    for (std::vector<FactorType>::const_iterator i = factorVec.begin();
         i != factorVec.end(); ++i) {
      SetFactor(currPos, *i, copy.GetFactor(currPos, *i));
    }
}


Phrase Phrase::GetSubString(const WordsRange &wordsRange) const
{
  Phrase retPhrase(wordsRange.GetNumWordsCovered());

  for (size_t currPos = wordsRange.GetStartPos() ; currPos <= wordsRange.GetEndPos() ; currPos++) {
    Word &word = retPhrase.AddWord();
    word = GetWord(currPos);
  }

  return retPhrase;
}

Phrase Phrase::GetSubString(const WordsRange &wordsRange, FactorType factorType) const
{
	Phrase retPhrase(wordsRange.GetNumWordsCovered());

	for (size_t currPos = wordsRange.GetStartPos() ; currPos <= wordsRange.GetEndPos() ; currPos++)
	{
		const Factor* f = GetFactor(currPos, factorType);
		Word &word = retPhrase.AddWord();
		word.SetFactor(factorType, f);
	}

	return retPhrase;
}

std::string Phrase::GetStringRep(const vector<FactorType> factorsToPrint) const
{
  stringstream strme;
  for (size_t pos = 0 ; pos < GetSize() ; pos++) {
    strme << GetWord(pos).GetString(factorsToPrint, (pos != GetSize()-1));
  }

  return strme.str();
}

Word &Phrase::AddWord()
{
  m_words.push_back(Word());
  return m_words.back();
}

void Phrase::Append(const Phrase &endPhrase)
{

  for (size_t i = 0; i < endPhrase.GetSize(); i++) {
    AddWord(endPhrase.GetWord(i));
  }
}

void Phrase::PrependWord(const Word &newWord)
{
  AddWord();

  // shift
  for (size_t pos = GetSize() - 1; pos >= 1; --pos) {
    const Word &word = m_words[pos - 1];
    m_words[pos] = word;
  }

  m_words[0] = newWord;
}

void Phrase::CreateFromString(const std::vector<FactorType> &factorOrder, const StringPiece &phraseString, const StringPiece &factorDelimiter)
{
  FactorCollection &factorCollection = FactorCollection::Instance();
  vector<string> sections = Tokenize(phraseString.as_string(), "\t");
  if (sections.size() > 1) {
    vector<string> words = Tokenize(sections[1], " ");
    vector<string>::const_iterator wordIt;
    for (wordIt = words.begin(); wordIt != words.end(); wordIt++) {
      vector<string> tokens = Tokenize(*wordIt, "|");
      if (tokens.size() == 2) {
        // counts are included
        m_context.insert(make_pair(tokens[0], Scan<int>(tokens[1])));
      } else if (tokens.size() == 1) {
        m_context.insert(make_pair(tokens[0], 1));
      }
    }
  }
  if (sections.empty()) {
    cerr << "Empty phrase." << endl;
    sections.push_back(""); // why does this happen?
  }

  for (util::TokenIter<util::AnyCharacter, true> word_it(phraseString, util::AnyCharacter(" \t")); word_it; ++word_it) {
    Word &word = AddWord();
    size_t index = 0;
    for (util::TokenIter<util::MultiCharacter, false> factor_it(*word_it, util::MultiCharacter(factorDelimiter)); 
        factor_it && (index < factorOrder.size()); 
        ++factor_it, ++index) {
      word[factorOrder[index]] = factorCollection.AddFactor(*factor_it);
    }
    if (index != factorOrder.size()) {
      TRACE_ERR( "[ERROR] Malformed input: '" << *word_it << "'" <<  std::endl
                 << "In '" << phraseString << "'" << endl
                 << "  Expected input to have words composed of " << factorOrder.size() << " factor(s) (form FAC1|FAC2|...)" << std::endl
                 << "  but instead received input with " << index << " factor(s).\n");
      abort();
    }
  }
}

void Phrase::CreateFromStringNewFormat(FactorDirection direction
                                       , const std::vector<FactorType> &factorOrder
                                       , const StringPiece &phraseString
                                       , const std::string & /*factorDelimiter */
                                       , Word &lhs)
{
  // parse
  vector<StringPiece> annotatedWordVector;
  for (util::TokenIter<util::AnyCharacter, true> it(phraseString, "\t "); it; ++it) {
    annotatedWordVector.push_back(*it);
  }
  // KOMMA|none ART|Def.Z NN|Neut.NotGen.Sg VVFIN|none
  //		to
  // "KOMMA|none" "ART|Def.Z" "NN|Neut.NotGen.Sg" "VVFIN|none"

  m_words.reserve(annotatedWordVector.size()-1);

  for (size_t phrasePos = 0 ; phrasePos < annotatedWordVector.size() -  1 ; phrasePos++) {
    StringPiece &annotatedWord = annotatedWordVector[phrasePos];
    bool isNonTerminal;
    if (annotatedWord.size() >= 2 && *annotatedWord.data() == '[' && annotatedWord.data()[annotatedWord.size() - 1] == ']') {
      // non-term
      isNonTerminal = true;

      size_t nextPos = annotatedWord.find('[', 1);
      CHECK(nextPos != string::npos);

      if (direction == Input)
        annotatedWord = annotatedWord.substr(1, nextPos - 2);
      else
        annotatedWord = annotatedWord.substr(nextPos + 1, annotatedWord.size() - nextPos - 2);
    } else {
      isNonTerminal = false;
    }

    Word &word = AddWord();
    word.CreateFromString(direction, factorOrder, annotatedWord, isNonTerminal);

  }

  // lhs
  const StringPiece &annotatedWord = annotatedWordVector.back();
  CHECK(annotatedWord.size() >= 2 && *annotatedWord.data() == '[' && annotatedWord.data()[annotatedWord.size() - 1] == ']');

  lhs.CreateFromString(direction, factorOrder, annotatedWord.substr(1, annotatedWord.size() - 2), true);
  assert(lhs.IsNonTerminal());
}

int Phrase::Compare(const Phrase &other) const
{
#ifdef min
#undef min
#endif
  size_t thisSize			= GetSize()
                        ,compareSize	= other.GetSize();
  if (thisSize != compareSize) {
    return (thisSize < compareSize) ? -1 : 1;
  }

  for (size_t pos = 0 ; pos < thisSize ; pos++) {
    const Word &thisWord	= GetWord(pos)
                            ,&otherWord	= other.GetWord(pos);
    int ret = Word::Compare(thisWord, otherWord);

    if (ret != 0)
      return ret;
  }

  return 0;
}


bool Phrase::Contains(const vector< vector<string> > &subPhraseVector
                      , const vector<FactorType> &inputFactor) const
{
  const size_t subSize = subPhraseVector.size()
                         ,thisSize= GetSize();
  if (subSize > thisSize)
    return false;

  // try to match word-for-word
  for (size_t currStartPos = 0 ; currStartPos < (thisSize - subSize + 1) ; currStartPos++) {
    bool match = true;

    for (size_t currFactorIndex = 0 ; currFactorIndex < inputFactor.size() ; currFactorIndex++) {
      FactorType factorType = inputFactor[currFactorIndex];
      for (size_t currSubPos = 0 ; currSubPos < subSize ; currSubPos++) {
        size_t currThisPos = currSubPos + currStartPos;
        const string &subStr	= subPhraseVector[currSubPos][currFactorIndex]
                                ,&thisStr	= GetFactor(currThisPos, factorType)->GetString();
        if (subStr != thisStr) {
          match = false;
          break;
        }
      }
      if (!match)
        break;
    }

    if (match)
      return true;
  }
  return false;
}

bool Phrase::IsCompatible(const Phrase &inputPhrase) const
{
  if (inputPhrase.GetSize() != GetSize()) {
    return false;
  }

  const size_t size = GetSize();

  const size_t maxNumFactors = MAX_NUM_FACTORS;
  for (size_t currPos = 0 ; currPos < size ; currPos++) {
    for (unsigned int currFactor = 0 ; currFactor < maxNumFactors ; currFactor++) {
      FactorType factorType = static_cast<FactorType>(currFactor);
      const Factor *thisFactor 		= GetFactor(currPos, factorType)
                                    ,*inputFactor	= inputPhrase.GetFactor(currPos, factorType);
      if (thisFactor != NULL && inputFactor != NULL && thisFactor != inputFactor)
        return false;
    }
  }
  return true;

}

bool Phrase::IsCompatible(const Phrase &inputPhrase, FactorType factorType) const
{
  if (inputPhrase.GetSize() != GetSize())	{
    return false;
  }
  for (size_t currPos = 0 ; currPos < GetSize() ; currPos++) {
    if (GetFactor(currPos, factorType) != inputPhrase.GetFactor(currPos, factorType))
      return false;
  }
  return true;
}

bool Phrase::IsCompatible(const Phrase &inputPhrase, const std::vector<FactorType>& factorVec) const
{
  if (inputPhrase.GetSize() != GetSize())	{
    return false;
  }
  for (size_t currPos = 0 ; currPos < GetSize() ; currPos++) {
    for (std::vector<FactorType>::const_iterator i = factorVec.begin();
         i != factorVec.end(); ++i) {
      if (GetFactor(currPos, *i) != inputPhrase.GetFactor(currPos, *i))
        return false;
    }
  }
  return true;
}

size_t Phrase::GetNumTerminals() const
{
  size_t ret = 0;

  for (size_t pos = 0; pos < GetSize(); ++pos) {
    if (!GetWord(pos).IsNonTerminal())
      ret++;
  }
  return ret;
}

void Phrase::InitializeMemPool()
{
}

void Phrase::FinalizeMemPool()
{
}

TO_STRING_BODY(Phrase);

// friend
ostream& operator<<(ostream& out, const Phrase& phrase)
{
//	out << "(size " << phrase.GetSize() << ") ";
  for (size_t pos = 0 ; pos < phrase.GetSize() ; pos++) {
    const Word &word = phrase.GetWord(pos);
    out << word;
  }
  return out;
}

}


