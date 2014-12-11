/**
 * This code is released under the
 * Apache License Version 2.0 http://www.apache.org/licenses/.
 *
 * (c) Daniel Lemire, http://lemire.me/en/
 */

#ifndef EWAH_H
#define EWAH_H

#include <algorithm>
#include <vector>
#include <sstream>
#include <iostream>
#include <memory>
#include "ewahutil.h"
#include "boolarray.h"

#include "runninglengthword.h"


using namespace std;

template<class uword>
class EWAHBoolArrayIterator;

template<class uword>
class EWAHBoolArraySetBitForwardIterator;

class BitmapStatistics;

template<class uword>
class EWAHBoolArrayRawIterator;

typedef uint32_t offsetType;

inline offsetType readOffsetType(const char * ptr, size_t pos) {
    return *reinterpret_cast<const offsetType *>(ptr + pos);
}

/**
 * This class is a compressed bitmap.
 * This is where compression
 * happens.
 * The underlying data structure is an STL vector.
 */
template<class uword = uint32_t>
class EWAHBoolArray {
public:
    EWAHBoolArray() :
        buffer(1, 0), sizeinbits(0), lastRLW(0) {
    }

    EWAHBoolArray(EWAHBoolArray&& rvalue) noexcept :
        buffer(std::move(rvalue.buffer)), sizeinbits(rvalue.sizeinbits), lastRLW(rvalue.lastRLW) {
    }

    static EWAHBoolArray bitmapOf(offsetType n, ...) {
    	EWAHBoolArray ans;
		va_list vl;
		va_start(vl, n);
		for (offsetType i = 0; i < n; i++) {
            ans.set(static_cast<offsetType>(va_arg(vl, int)));
	    }
	    va_end(vl);
	    return ans;
	}

    /**
     * Query the value of bit i. This runs in time proportional to
     * the size of the bitmap. This is not meant to be use in
     * a performance-sensitive context.
     *
     *  (This implementation is based on zhenjl's Go version of JavaEWAH.)
     *
     */
    bool get(const offsetType pos) const {
        if ( pos >= static_cast<offsetType>(sizeinbits) )
                return false;
        const offsetType wordpos = pos / wordinbits;
        offsetType WordChecked = 0;
        EWAHBoolArrayRawIterator<uword> j = raw_iterator();
        while(j.hasNext()) {
        	BufferedRunningLengthWord<uword> & rle = j.next();
        	WordChecked += static_cast<offsetType>( rle.getRunningLength());
        	if(wordpos < WordChecked)
        		return rle.getRunningBit();
        	if(wordpos < WordChecked + rle.getNumberOfLiteralWords() ) {
        		const uword w = j.dirtyWords()[wordpos - WordChecked];
                return (w & (static_cast<uword>(1) << (pos % wordinbits))) != 0;
        	}
        	WordChecked += static_cast<offsetType>(rle.getNumberOfLiteralWords());
        }
        return false;
      }


    /**
     * Set the ith bit to true (starting at zero).
     * Auto-expands the bitmap. It has constant running time complexity.
     * Note that you must set the bits in increasing order:
     * set(1), set(2) is ok; set(2), set(1) is not ok.
     * set(100), set(100) is also not ok.
     *
     * Note: by design EWAH is not an updatable data structure in
     * the sense that once bit 1000 is set, you cannot change the value
     * of bits 0 to 1000.
     *
     * Returns true if the value of the bit was changed, and false otherwise.
     * (In practice, if you set the bits in strictly increasing order, it
     * should always return true.)
     */
    bool set(offsetType i);

    /**
     * Transform into a string that presents a list of set bits.
     * The running time is linear in the compressed size of the bitmap.
     */
    operator string() const {
		stringstream ss;
		ss << *this;
		return ss.str();
	}
    friend ostream& operator<< (ostream &out, const EWAHBoolArray &a) {

    	out<<"{";
		for (EWAHBoolArray::const_iterator i = a.begin(); i != a.end(); ) {
			out<<*i;
			++i;
			if( i != a.end())
				out << ",";
		}
		out <<"}";

    	return out;
    }
    /**
     * Make sure the two bitmaps have the same size (padding with zeroes
     * if necessary). It has constant running time complexity.
     */
    void makeSameSize(EWAHBoolArray & a) {
        if (a.sizeinbits < sizeinbits)
            a.padWithZeroes(sizeinbits);
        else if (sizeinbits < a.sizeinbits)
            padWithZeroes(a.sizeinbits);
    }

    enum {
        RESERVEMEMORY = true
    }; // for speed

    typedef EWAHBoolArraySetBitForwardIterator<uword> const_iterator;

    /**
     * Returns an iterator that can be used to access the position of the
     * set bits. The running time complexity of a full scan is proportional to the number
     * of set bits: be aware that if you have long strings of 1s, this can be
     * very inefficient.
     *
     * It can be much faster to use the toArray method if you want to
     * retrieve the set bits.
     */
    const_iterator begin() const {
        return EWAHBoolArraySetBitForwardIterator<uword> (buffer);
    }

    /**
     * Basically a bogus iterator that can be used together with begin()
     * for constructions such as for(EWAHBoolArray<uword>::iterator i = b.begin(); i!=b.end(); ++i) {}
     */
    const_iterator end() const {
        return EWAHBoolArraySetBitForwardIterator<uword> (buffer, buffer.size());
    }

    /**
     * Retrieve the set bits. Can be much faster than iterating through
     * the set bits with an iterator.
     */
    vector<offsetType> toArray() const;

    /**
    * Iterate over the set bits.
    */
    template<typename Function>
    void iterateSetBits(Function function) const;

    /**
     * computes the logical and with another compressed bitmap
     * answer goes into container
     * Running time complexity is proportional to the sum of the compressed
     * bitmap sizes.
     */
    void logicaland(EWAHBoolArray &a, EWAHBoolArray &container);

    /**
     * computes the logical or with another compressed bitmap
     * answer goes into container
     * Running time complexity is proportional to the sum of the compressed
     * bitmap sizes.
     */
    void logicalor(EWAHBoolArray &a, EWAHBoolArray &container);


    /**
     * computes the logical xor with another compressed bitmap
     * answer goes into container
     * Running time complexity is proportional to the sum of the compressed
     * bitmap sizes.
     */
    void logicalxor(EWAHBoolArray &a, EWAHBoolArray &container);

    /**
     * clear the content of the bitmap. It does not
     * release the memory.
     */
    void reset() {
        buffer.clear();
        buffer.push_back(0);
        sizeinbits = 0;
        lastRLW = 0;
    }

    /**
     * convenience method.
     *
     * returns the number of words added (storage cost increase)
     */
    inline offsetType addWord(const uword newdata,
            const offsetType bitsthatmatter = 8 * sizeof(uword));

    inline void printout(ostream &o = cout) {
        toBoolArray().printout(o);
    }

    /**
     * Prints a verbose description of the content of the compressed bitmap.
     */
    void debugprintout() const;

    /**
     * Return the size in bits of this bitmap (this refers
     * to the uncompressed size in bits).
     */
    inline offsetType sizeInBits() const {
        return sizeinbits;
    }

    /**
     * set size in bits. This does not affect the compressed size. It
     * runs in constant time.
     */
    inline void setSizeInBits(const offsetType size) {
        sizeinbits = size;
    }

    /**
     * Return the size of the buffer in bytes. This
     * is equivalent to the storage cost, minus some overhead.
     */
    inline offsetType sizeInBytes() const {
        return buffer.size() * sizeof(uword);
    }

    /**
     * same as addEmptyWord, but you can do several in one shot!
     * returns the number of words added (storage cost increase)
     */
    offsetType addStreamOfEmptyWords(const bool v, offsetType number);

    /**
     * add a stream of dirty words, returns the number of words added
     * (storage cost increase)
     */
    offsetType addStreamOfDirtyWords(const uword * v, const offsetType number);

    /**
     * make sure the size of the array is totalbits bits by padding with zeroes.
     * returns the number of words added (storage cost increase)
     */
    offsetType padWithZeroes(const offsetType totalbits);

    /**
     * Compute the size on disk assuming that it was saved using
     * the method "save".
     */
    offsetType sizeOnDisk() const;

    /**
     * Save this bitmap to a stream. The file format is
     * | sizeinbits | buffer lenth | buffer content|
     * the sizeinbits part can be omitted if "savesizeinbits=false".
     * Both sizeinbits and buffer length are saved using the offsetType data
     * type which is typically a 32-bit unsigned integer for 32-bit CPUs
     * and a 64-bit unsigned integer for 64-bit CPUs.
     * Note that this format is machine-specific. Note also
     * that the word size is not saved. For robust persistent
     * storage, you need to save this extra information elsewhere.
     */
    void write(ostream & out, const bool savesizeinbits = true) const;

    /**
    * Save this bitmap to a string in a sparse, appendable format.
    * The format is:
    * | offset:offsetType | bufferLength:offsetType | buffer content | bufferLength:offsetType |
    *
    * The "end offset" is provided when serializing. It can be used to append
    * to a content that a previous stream has already written to.
    */
    void appendToString(std::string * result, offsetType end_offset) const;

    static void concatStreams(std::string * target, const char * newStream, size_t newStreamSize) {
        target->reserve(target->size() + newStreamSize);
        target->append(newStream, newStreamSize);
    }

    /**
    * The padding necessary to align to a word.
    */
    static offsetType padToWord(offsetType length) {
        offsetType missingbits = wordinbits - (length % wordinbits);
        if (missingbits % wordinbits > 0) {
            return missingbits;
        } else {
            return 0;
        }
    }

    /**
     * This only writes the content of the buffer (see write()) method.
     * It is for advanced users.
     */
    void writeBuffer(ostream & out) const;

    /**
     * size (in words) of the underlying STL vector.
     */
    offsetType bufferSize() const {
        return buffer.size();
    }

    /**
     * this is the counterpart to the write method.
     * if you set savesizeinbits=false, then you are responsible
     * for setting the value fo the attribute sizeinbits (see method setSizeInBits).
     */
    void read(istream & in, const bool savesizeinbits = true);

    /**
    * Read in stream format. I.e., the size in bits is at the end of the stream.
    */
    void readStream(const char * in, const offsetType input_length);


    void read(const char * in);

    /**
     * read the buffer from a stream, see method writeBuffer.
     * this is for advanced users.
     */
    void readBuffer(istream & in, const offsetType buffersize);

    bool operator==(const EWAHBoolArray & x) const;

    bool operator!=(const EWAHBoolArray & x) const;

    bool operator==(const BoolArray<uword> & x) const;

    bool operator!=(const BoolArray<uword> & x) const;

    /**
     * Iterate over the uncompressed words.
     * Can be considerably faster than begin()/end().
     * Running time complexity of a full scan is proportional to the
     * uncompressed size of the bitmap.
     */
    EWAHBoolArrayIterator<uword> uncompress() const ;

    /**
     * To iterate over the compressed data.
     * Can be faster than any other iterator.
     * Running time complexity of a full scan is proportional to the
     * compressed size of the bitmap.
     */
    EWAHBoolArrayRawIterator<uword> raw_iterator() const ;

    /**
     * Appends the content of some other compressed bitmap
     * at the end of the current bitmap.
     */
    void append(const EWAHBoolArray & x);

    /**
     * For research purposes. This computes the number of
     * dirty words and the number of compressed words.
     */
    BitmapStatistics computeStatistics() const;

    /**
     * For convenience, this fully uncompresses the bitmap.
     * Not fast!
     */
    BoolArray<uword> toBoolArray() const;

    /**
     * Convert to a list of positions of "set" bits.
     * The recommender container is vector<offsetType>.
     */
    template<class container>
    void appendRowIDs(container & out, const offsetType offset = 0) const;

    /**
     * Convert to a list of positions of "set" bits.
     * The recommender container is vector<offsetType>.
     * (alias for appendRowIDs).
     */
    template<class container>
    void appendSetBits(container & out, const offsetType offset = 0) const {
        return appendRowIDs(out, offset);
    }

    /**
     * Returns the number of bits set to the value 1.
     * The running time complexity is proportional to the
     * compressed size of the bitmap.
     *
     * This is sometimes called the cardinality.
     */
    offsetType numberOfOnes() const;

    /**
     * Swap the content of this bitmap with another bitmap.
     * No copying is done. (Running time complexity is constant.)
     */
    void swap(EWAHBoolArray & x);

    const vector<uword> & getBuffer() const {
        return buffer;
    }
    ;
    enum {
        wordinbits = sizeof(uword) * 8
    };

    /**
     *Please don't copy your bitmaps! The running time
     * complexity of a copy is the size of the compressed bitmap.
     **/
    EWAHBoolArray(const EWAHBoolArray& other) :
        buffer(other.buffer), sizeinbits(other.sizeinbits),
                lastRLW(other.lastRLW) {
        //ASSERT(buffer.size()<=1,"You are trying to copy the bitmap, a terrible idea in general, for performance reasons.");// performance assert!
    }

    /**
     * Copies the content of one bitmap onto another. Running time complexity
     * is proportional to the size of the compressed bitmap.
     * please, never hard-copy this object. Use the swap method if you must.
     */
    EWAHBoolArray & operator=(const EWAHBoolArray & x) {
        buffer = x.buffer;
        sizeinbits = x.sizeinbits;
        lastRLW = x.lastRLW;
        return *this;
    }

    EWAHBoolArray & operator=(EWAHBoolArray && rvalue) noexcept {
        buffer = std::move(rvalue.buffer);
        sizeinbits = rvalue.sizeinbits;
        lastRLW = rvalue.lastRLW;
        return *this;
    }

    /**
     * This is equivalent to the operator =. It is used
     * to keep in mind that assignment can be expensive.
     *
     *if you don't care to copy the bitmap (performance-wise), use this!
     */
    void expensive_copy(const EWAHBoolArray & x) {
        buffer = x.buffer;
        sizeinbits = x.sizeinbits;
        lastRLW = x.lastRLW;
    }

    /**
     * Write the logical not of this bitmap in the provided container.
     */
    void logicalnot(EWAHBoolArray & x) const;

    /**
     * Apply the logical not operation on this bitmap.
     * Running time complexity is proportional to the compressed size of the bitmap.
     * The current bitmap is not modified.
     */
    void inplace_logicalnot();

private:

    // addStreamOfEmptyWords but does not return the cost increase,
    // does not update sizeinbits and does not check that number>0
    void fastaddStreamOfEmptyWords(const bool v, offsetType number);

    // private because does not increment the size in bits
    // returns the number of words added (storage cost increase)
    inline offsetType addLiteralWord(const uword newdata);

    // private because does not increment the size in bits
    // returns the number of words added (storage cost increase)
    offsetType addEmptyWord(const bool v);
    // this second version "might" be faster if you hate OOP.
    // in my tests, it turned out to be slower!
    // private because does not increment the size in bits
    //inline void addEmptyWordStaticCalls(bool v);

    vector<uword> buffer;
    offsetType sizeinbits;
    offsetType lastRLW;
};

/**
 * Iterate over words of bits from a compressed bitmap.
 */
template<class uword>
class EWAHBoolArrayIterator {
public:
    /**
     * is there a new word?
     */
    bool hasNext() const {
        return pointer < myparent.size();
    }

    /**
     * return next word.
     */
    uword next() {
        uword returnvalue;
        if (compressedwords < rl) {
            ++compressedwords;
            if (b)
                returnvalue = notzero;
            else
                returnvalue = zero;
        } else {
            assert(literalwords < lw);
            ++literalwords;
            ++pointer;
            assert(pointer < myparent.size());
            returnvalue = myparent[pointer];
        }
        if ((compressedwords == rl) && (literalwords == lw)) {
            ++pointer;
            if (pointer < myparent.size())
                readNewRunningLengthWord();
        }
        return returnvalue;
    }

    EWAHBoolArrayIterator(const EWAHBoolArrayIterator<uword> & other) :
        pointer(other.pointer), myparent(other.myparent),
                compressedwords(other.compressedwords),
                literalwords(other.literalwords), rl(other.rl), lw(other.lw),
                b(other.b) {
    }

    static const uword zero = 0;
    static const uword notzero = static_cast<uword> (~zero);
private:
    EWAHBoolArrayIterator(const vector<uword> & parent);
    void readNewRunningLengthWord();
    friend class EWAHBoolArray<uword> ;
    offsetType pointer;
    const vector<uword> & myparent;
    uword compressedwords;
    uword literalwords;
    uword rl, lw;
    bool b;
};

/**
 * Used to go through the set bits. Not optimally fast, but convenient.
 */
template<class uword>
class EWAHBoolArraySetBitForwardIterator {
public:
    enum {
        wordinbits = sizeof(uword) * 8
    };
    typedef forward_iterator_tag iterator_category;
    typedef offsetType * pointer;
    typedef offsetType & reference_type;
    typedef offsetType value_type;
    typedef ptrdiff_t difference_type;
    typedef EWAHBoolArraySetBitForwardIterator<uword> type_of_iterator;

    /**
     * Provides the location of the set bit.
     */
    offsetType operator*() const {
        return currentrunoffset + offsetofpreviousrun;
    }

    // this can be expensive
    difference_type operator-(const type_of_iterator& o) {
        type_of_iterator& smaller = *this < o ? *this : o;
        type_of_iterator& bigger = *this >= o ? *this : o;
        if (smaller.mpointer == smaller.buffer.size())
            return 0;
        difference_type absdiff = static_cast<difference_type> (0);
        EWAHBoolArraySetBitForwardIterator<uword> buf(smaller);
        while (buf != bigger) {
            ++absdiff;
            ++buf;
        }
        if (*this < o)
            return absdiff;
        else
            return -absdiff;
    }

    bool operator<(const type_of_iterator& o) {
        if (buffer != o.buffer)
            return false;
        if (mpointer == buffer.size())
            return false;
        if (o.mpointer == o.buffer.size())
            return true;
        if (offsetofpreviousrun < o.offsetofpreviousrun)
            return true;
        if (offsetofpreviousrun > o.offsetofpreviousrun)
            return false;
        if (currentrunoffset < o.currentrunoffset)
            return true;
        return false;
    }
    bool operator<=(const type_of_iterator& o) {
        return ((*this) < o) || ((*this) == o);
    }

    bool operator>(const type_of_iterator& o) {
        return !((*this) <= o);
    }

    bool operator>=(const type_of_iterator& o) {
        return !((*this) < o);
    }

    EWAHBoolArraySetBitForwardIterator & operator++() {
        ++currentrunoffset;
        advanceToNextSetBit();
        return *this;
    }
    EWAHBoolArraySetBitForwardIterator operator++(int) {
        EWAHBoolArraySetBitForwardIterator old(*this);
        ++currentrunoffset;
        advanceToNextSetBit();
        return old;
    }
    bool operator==(const EWAHBoolArraySetBitForwardIterator<uword> & o) {
        // if they are both over, return true
        if ((mpointer == buffer.size()) && (o.mpointer == o.buffer.size()))
            return true;
        return (&buffer == &o.buffer) && (mpointer == o.mpointer)
                && (offsetofpreviousrun == o.offsetofpreviousrun)
                && (currentrunoffset == o.currentrunoffset);
    }
    bool operator!=(const EWAHBoolArraySetBitForwardIterator<uword> & o) {
        // if they are both over, return false
        if ((mpointer == buffer.size()) && (o.mpointer == o.buffer.size()))
            return false;
        return (&buffer != &o.buffer) || (mpointer != o.mpointer)
                || (offsetofpreviousrun != o.offsetofpreviousrun)
                || (currentrunoffset != o.currentrunoffset);
    }

    EWAHBoolArraySetBitForwardIterator(
            const EWAHBoolArraySetBitForwardIterator & o) :
        buffer(o.buffer), mpointer(o.mpointer),
                offsetofpreviousrun(o.offsetofpreviousrun),
                currentrunoffset(o.currentrunoffset), rlw(o.rlw) {
    }

private:

    bool advanceToNextSetBit() {
        if (mpointer == buffer.size())
            return false;
        if (currentrunoffset < static_cast<offsetType> (rlw.getRunningLength()
                * wordinbits)) {
            if (rlw.getRunningBit())
                return true;// nothing to do
            currentrunoffset = static_cast<offsetType> (rlw.getRunningLength()
                    * wordinbits);//skipping
        }
        while (true) {
            const offsetType
                    indexoflitword =
                            static_cast<offsetType> ((currentrunoffset
                                    - rlw.getRunningLength() * wordinbits)
                                    / wordinbits);
            if (indexoflitword >= rlw.getNumberOfLiteralWords()) {
                if (advanceToNextRun())
                    return advanceToNextSetBit();
                else {
                    return false;
                }
            }

            if (usetrailingzeros) {

                const offsetType tinwordpointer =
                        static_cast<offsetType> ((currentrunoffset
                                - rlw.getRunningLength() * wordinbits)
                                % wordinbits);
                const uword modcurrentword =
                        static_cast<uword> (buffer[mpointer + 1
                                + indexoflitword] >> tinwordpointer);
                if (modcurrentword != 0) {
                    currentrunoffset
                            += static_cast<offsetType> (numberOfTrailingZeros(
                                    modcurrentword));
                    return true;
                } else {
                    currentrunoffset += wordinbits - tinwordpointer;
                }
            } else {
                const uword currentword = buffer[mpointer + 1 + indexoflitword];
                for (offsetType inwordpointer =
                        static_cast<offsetType> ((currentrunoffset
                                - rlw.getRunningLength() * wordinbits)
                                % wordinbits); inwordpointer < wordinbits; ++inwordpointer, ++currentrunoffset) {
                    if ((currentword
                            & (static_cast<uword> (1) << inwordpointer)) != 0)
                        return true;
                }
            }
        }
    }

    enum {
        usetrailingzeros = true
    };// optimization option

    bool advanceToNextRun() {
        offsetofpreviousrun += currentrunoffset;
        currentrunoffset = 0;
        mpointer += static_cast<offsetType> (1 + rlw.getNumberOfLiteralWords());
        if (mpointer < buffer.size()) {
            rlw.mydata = buffer[mpointer];
        } else {
            return false;
        }
        return true;
    }

    EWAHBoolArraySetBitForwardIterator(const vector<uword> & parent,
            offsetType startpointer = 0) :
        buffer(parent), mpointer(startpointer), offsetofpreviousrun(0),
                currentrunoffset(0), rlw(0) {
        if (mpointer < buffer.size()) {
            rlw.mydata = buffer[mpointer];
            advanceToNextSetBit();
        }
    }

    const vector<uword> & buffer;
    offsetType mpointer;
    offsetType offsetofpreviousrun;
    offsetType currentrunoffset;
    friend class EWAHBoolArray<uword> ;
    ConstRunningLengthWord<uword> rlw;
};

/**
 * This object is returned by the compressed bitmap as a
 * statistical descriptor.
 */
class BitmapStatistics {
public:
    BitmapStatistics() :
        totalliteral(0), totalcompressed(0), runningwordmarker(0),
                maximumofrunningcounterreached(0) {
    }
    offsetType getCompressedSize() const {
        return totalliteral + runningwordmarker;
    }
    offsetType getUncompressedSize() const {
        return totalliteral + totalcompressed;
    }
    offsetType getNumberOfDirtyWords() const {
        return totalliteral;
    }
    offsetType getNumberOfCleanWords() const {
        return totalcompressed;
    }
    offsetType getNumberOfMarkers() const {
        return runningwordmarker;
    }
    offsetType getOverRuns() const {
        return maximumofrunningcounterreached;
    }
    offsetType totalliteral;
    offsetType totalcompressed;
    offsetType runningwordmarker;
    offsetType maximumofrunningcounterreached;
};

template<class uword>
bool EWAHBoolArray<uword>::set(offsetType i) {
    if(i < sizeinbits) return false;
    const offsetType dist = (i + wordinbits) / wordinbits - (sizeinbits
            + wordinbits - 1) / wordinbits;
    sizeinbits = i + 1;
    if (dist > 0) {// easy
        if(dist>1) fastaddStreamOfEmptyWords(false, dist - 1);
        addLiteralWord(
                static_cast<uword> (static_cast<uword> (1) << (i % wordinbits)));
        return true;
    }
    RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
    if (lastRunningLengthWord.getNumberOfLiteralWords() == 0) {
        lastRunningLengthWord.setRunningLength(
                static_cast<uword> (lastRunningLengthWord.getRunningLength()
                        - 1));
        addLiteralWord(
                static_cast<uword> (static_cast<uword> (1) << (i % wordinbits)));
        return true;
    }
    buffer[buffer.size() - 1] |= static_cast<uword> (static_cast<uword> (1)
            << (i % wordinbits));
    // check if we just completed a stream of 1s
    if (buffer[buffer.size() - 1] == static_cast<uword> (~0)) {
        // we remove the last dirty word
        buffer[buffer.size() - 1] = 0;
        buffer.resize(buffer.size() - 1);
        lastRunningLengthWord.setNumberOfLiteralWords(
                static_cast<uword> (lastRunningLengthWord.getNumberOfLiteralWords()
                        - 1));
        // next we add one clean word
        addEmptyWord(true);
    }
    return true;
}

template<class uword>
void EWAHBoolArray<uword>::inplace_logicalnot() {
    offsetType pointer(0), lastrlw(0);
    while (pointer < buffer.size()) {
        RunningLengthWord<uword> rlw(buffer[pointer]);
        lastrlw = pointer;// we save this up
        if (rlw.getRunningBit())
            rlw.setRunningBit(false);
        else
            rlw.setRunningBit(true);
        ++pointer;
        for (offsetType k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
            buffer[pointer] = static_cast<uword>(~buffer[pointer]);
            ++pointer;
        }
    }
    if(sizeinbits % wordinbits != 0){
        RunningLengthWord<uword> rlw(buffer[lastrlw]);
        assert(rlw.getNumberOfLiteralWords() + rlw.getRunningLength() > 0);
        const uword maskbogus = (static_cast<uword>(1) << (sizeinbits % wordinbits)) - 1;
        if(rlw.getNumberOfLiteralWords()>0) {// easy case
            buffer[lastrlw + 1 + rlw.getNumberOfLiteralWords() - 1 ] &= maskbogus;
        } else if(rlw.getRunningBit()) {
            assert(rlw.getNumberOfLiteralWords() > 0);
            rlw.setNumberOfLiteralWords(rlw.getNumberOfLiteralWords() - 1);
            addLiteralWord(maskbogus);
        }
    }
}


template<class uword>
offsetType EWAHBoolArray<uword>::numberOfOnes() const {
    offsetType tot(0);
    offsetType pointer(0);
    while (pointer < buffer.size()) {
        ConstRunningLengthWord<uword> rlw(buffer[pointer]);
        if (rlw.getRunningBit()) {
            tot += static_cast<offsetType>(rlw.getRunningLength() * wordinbits);
        }
        ++pointer;
        for (offsetType k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
            assert(countOnes(buffer[pointer]) < 64);
            tot += countOnes(buffer[pointer]);
            ++pointer;
        }
    }
    return tot;
}



template<class uword>
vector<offsetType> EWAHBoolArray<uword>::toArray() const {
    vector < offsetType > ans;
    iterateSetBits([&ans] (offsetType pos) {
      ans.push_back(pos);
    });
    return ans;
}


template<class uword>
template<typename Function>
void EWAHBoolArray<uword>::iterateSetBits(Function callable) const {
    offsetType pos(0);
    offsetType pointer(0);
    while (pointer < buffer.size()) {
        ConstRunningLengthWord<uword> rlw(buffer[pointer]);
        if (rlw.getRunningBit()) {
            for (offsetType k = 0; k < rlw.getRunningLength() * wordinbits; ++k, ++pos) {
                callable(pos);
            }
        } else {
            pos += static_cast<offsetType>(rlw.getRunningLength() * wordinbits);
        }
        ++pointer;
        const bool usetrailing = true; //optimization
        for (offsetType k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
            if (usetrailing) {
                uword myword = buffer[pointer];
                while (myword != 0) {
                  offsetType ntz =  numberOfTrailingZeros (myword);
                  callable(pos + ntz);
                  myword ^= (static_cast<uword>(1) << ntz);
                }
                pos += wordinbits;
            } else {
                for (int c = 0; c < wordinbits; ++c, ++pos)
                    if ((buffer[pointer] & (static_cast<uword> (1) << c)) != 0) {
                        callable(pos);
                    }
            }
            ++pointer;
        }
    }
}

template<class uword>
void EWAHBoolArray<uword>::logicalnot(EWAHBoolArray & x) const {
    x.reset();
    x.buffer.reserve(buffer.size());
    EWAHBoolArrayRawIterator<uword> i = this->raw_iterator();
    if(!i.hasNext()) return;// nothing to do
    while (true) {
        BufferedRunningLengthWord<uword> & rlw = i.next();
        if (i.hasNext()) {
            x.addStreamOfEmptyWords(!rlw.getRunningBit(),
                    rlw.getRunningLength());
            if (rlw.getNumberOfLiteralWords() > 0) {
                const uword * dw = i.dirtyWords();
                for (offsetType k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
                    x.addLiteralWord(~dw[k]);
                }
            }
        } else {
            assert(rlw.getNumberOfLiteralWords() + rlw.getRunningLength() > 0);
            if(rlw.getNumberOfLiteralWords() == 0) {
                if((this->sizeinbits % wordinbits != 0) && !rlw.getRunningBit()) {
                    x.addStreamOfEmptyWords(!rlw.getRunningBit(),
                            rlw.getRunningLength() - 1);
                    const uword maskbogus = (static_cast<uword>(1) << (this->sizeinbits % wordinbits)) - 1;
                    x.addLiteralWord(maskbogus);
                    break;
                } else {
                    x.addStreamOfEmptyWords(!rlw.getRunningBit(),
                            rlw.getRunningLength());
                    break;
                }
            }
            x.addStreamOfEmptyWords(!rlw.getRunningBit(),
                                rlw.getRunningLength());
            const uword * dw = i.dirtyWords();
            for (offsetType k = 0; k < rlw.getNumberOfLiteralWords()  - 1; ++k) {
                                x.addLiteralWord(~dw[k]);
            }
            const uword maskbogus = (this->sizeinbits % wordinbits != 0) ? (static_cast<uword>(1) << (this->sizeinbits % wordinbits)) - 1 : ~static_cast<uword>(0);
            x.addLiteralWord((~dw[rlw.getNumberOfLiteralWords()  - 1]) & maskbogus);
            break;
        }
    }
    x.sizeinbits = this->sizeinbits;
}

template<class uword>
offsetType EWAHBoolArray<uword>::addWord(const uword newdata,
        const offsetType bitsthatmatter) {
    sizeinbits += bitsthatmatter;
    if (newdata == 0) {
        return addEmptyWord(0);
    } else if (newdata == static_cast<uword> (~0)) {
        return addEmptyWord(1);
    } else {
        return addLiteralWord(newdata);
    }
}

template<class uword>
inline void EWAHBoolArray<uword>::writeBuffer(ostream & out) const {
    if (!buffer.empty())
        out.write(reinterpret_cast<const char *> (&buffer[0]),
                sizeof(uword) * buffer.size());
}

template<class uword>
inline void EWAHBoolArray<uword>::readBuffer(istream & in,
        const offsetType buffersize) {
    buffer.resize(buffersize);
    if (buffersize > 0)
        in.read(reinterpret_cast<char *> (&buffer[0]),
                sizeof(uword) * buffersize);
}


template<class uword>
void EWAHBoolArray<uword>::write(ostream & out, const bool savesizeinbits) const {
    if (savesizeinbits)
        out.write(reinterpret_cast<const char *> (&sizeinbits),
                sizeof(sizeinbits));
    const offsetType buffersize = buffer.size();
    out.write(reinterpret_cast<const char *> (&buffersize), sizeof(buffersize));
    if (buffersize > 0)
        out.write(reinterpret_cast<const char *> (&buffer[0]),
                static_cast<streamsize> (sizeof(uword) * buffersize));
}

template<class uword>
void EWAHBoolArray<uword>::appendToString(std::string * result, offsetType offset) const {
    const offsetType buffersize = buffer.size();

    result->reserve(result->size() + buffersize * sizeof(uword) + 3 * sizeof(offsetType));

    result->append(reinterpret_cast<const char *> (&offset), sizeof(offsetType));

    result->append(reinterpret_cast<const char *> (&buffersize), sizeof(offsetType));

    result->append(reinterpret_cast<const char *> (&sizeinbits), sizeof(offsetType));

    if (!buffer.empty()) {
        result->append(
            reinterpret_cast<const char *>(&buffer[0]),
            static_cast<size_t> (sizeof(uword) * buffer.size())
        );
    }
}

template<class uword>
void EWAHBoolArray<uword>::read(istream & in, const bool savesizeinbits) {
    if (savesizeinbits)
        in.read(reinterpret_cast<char *> (&sizeinbits), sizeof(sizeinbits));
    else
        sizeinbits = 0;
    offsetType buffersize(0);
    in.read(reinterpret_cast<char *> (&buffersize), sizeof(buffersize));
    buffer.resize(buffersize);
    if (buffersize > 0)
        in.read(reinterpret_cast<char *> (&buffer[0]),
                static_cast<streamsize> (sizeof(uword) * buffersize));
}

template<class uword>
void EWAHBoolArray<uword>::read(const char * in) {
    offsetType buffersize{0};
    buffersize = *reinterpret_cast<const offsetType *>(in);
    sizeinbits = 0;
    buffer.clear();
    buffer.reserve(buffersize);
    if (buffersize > 0) {
        buffer.insert(
            buffer.end(),
            in + sizeof(offsetType),
            in + sizeof(offsetType) + sizeof(uword) * buffersize
        );
    }
}

template<class uword>
inline offsetType appendToBuffer(vector<uword> & buffer, const char * in, const offsetType bufferSize) {
    const offsetType bufferSizeBytes = bufferSize * static_cast<offsetType>(sizeof(uword));
    buffer.clear();
    buffer.reserve(bufferSize);
    buffer.insert(
        buffer.end(),
        reinterpret_cast<const uword *>(in),
        reinterpret_cast<const uword *>(in + bufferSizeBytes)
    );
    return bufferSizeBytes;
}

template<class uword>
void EWAHBoolArray<uword>::readStream(const char * in, const offsetType input_length) {
    reset();
    size_t currentPos{0};
    EWAHBoolArray<uword> other{};

    while (currentPos < input_length) {
        const offsetType offset = readOffsetType(in, currentPos);
        assert(offset % wordinbits == 0);
        currentPos += sizeof(offsetType);

        const offsetType bufferSize = readOffsetType(in, currentPos);
        currentPos += sizeof(offsetType);
        const offsetType currentSizeinbits = readOffsetType(in, currentPos);
        currentPos += sizeof(offsetType);

        offsetType bufferSizeBytes;

        if (offset == 0) {
            sizeinbits = currentSizeinbits;
            bufferSizeBytes = appendToBuffer(buffer, in + currentPos, bufferSize);
        } else {
            other.reset();
            other.sizeinbits = currentSizeinbits;
            bufferSizeBytes = appendToBuffer(other.buffer, in + currentPos, bufferSize);
            padWithZeroes(offset);
            append(other);
        }

        currentPos += bufferSizeBytes;
    }
}

template<class uword>
offsetType EWAHBoolArray<uword>::addLiteralWord(const uword newdata) {
    RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
    uword numbersofar = lastRunningLengthWord.getNumberOfLiteralWords();
    if (numbersofar >= RunningLengthWord<uword>::largestliteralcount) {//0x7FFF) {
        buffer.push_back(0);
        lastRLW = buffer.size() - 1;
        RunningLengthWord<uword> lastRunningLengthWord2(buffer[lastRLW]);
        lastRunningLengthWord2.setNumberOfLiteralWords(1);
        buffer.push_back(newdata);
        return 2;
    }
    lastRunningLengthWord.setNumberOfLiteralWords(
            static_cast<uword> (numbersofar + 1));
    assert(lastRunningLengthWord.getNumberOfLiteralWords() == numbersofar + 1);
    buffer.push_back(newdata);
    return 1;
}

template<class uword>
offsetType EWAHBoolArray<uword>::padWithZeroes(const offsetType totalbits) {
	offsetType wordsadded = 0;
    assert(totalbits >= sizeinbits);
	if ( totalbits <= sizeinbits )
		return wordsadded;

    offsetType missingbits = totalbits - sizeinbits;


	RunningLengthWord<uword> rlw( buffer[lastRLW] );
	if ( rlw.getNumberOfLiteralWords() > 0 )
	{
		// Consume trailing zeroes of trailing literal word (past sizeinbits)
		offsetType remain = sizeinbits % wordinbits;
		if ( remain > 0 )	// Is last word partial?
		{
			offsetType avail = wordinbits - remain;
			if ( avail > 0 )
			{
				if ( missingbits > avail ) {
					missingbits -= avail;
				} else {
					missingbits = 0;
				}
				sizeinbits += avail;
			}
		}
	}

	if ( missingbits > 0 )
	{
		offsetType wordstoadd = missingbits / wordinbits;
		if ( (missingbits % wordinbits) != 0)
			++wordstoadd;

		wordsadded = addStreamOfEmptyWords( false, wordstoadd );
	}

    assert(sizeinbits >= totalbits);
    assert(sizeinbits <= totalbits + wordinbits);
    sizeinbits = totalbits;
    return wordsadded;
}

/**
 * This is a low-level iterator.
 */

template<class uword = offsetType>
class EWAHBoolArrayRawIterator {
public:
    EWAHBoolArrayRawIterator(const EWAHBoolArray<uword> & p) :
        pointer(0), myparent(&p.getBuffer()), rlw((*myparent)[pointer]) { //RunningLength(0), NumberOfLiteralWords(0), Bit(0) {
        if (verbose) {
            cout << "created a new raw iterator over buffer of size  "
                    << myparent->size() << endl;
        }
    }
    EWAHBoolArrayRawIterator(const EWAHBoolArrayRawIterator & o) :
        pointer(o.pointer), myparent(o.myparent), rlw(o.rlw) {
    }

    bool hasNext() const {
        if (verbose)
            cout << "call to hasNext, pointer is at " << pointer
                    << ", parent.size()= " << myparent->size() << endl;
        return pointer < myparent->size();
    }

    BufferedRunningLengthWord<uword> & next() {
        assert(pointer < myparent->size());
        rlw.read((*myparent)[pointer]);
        pointer = static_cast<offsetType> (pointer + rlw.getNumberOfLiteralWords()
                + 1);
        return rlw;
    }

    const uword * dirtyWords() const {
        assert(pointer > 0);
        assert(pointer >= rlw.getNumberOfLiteralWords());
        return &(myparent->at(
                static_cast<offsetType> (pointer - rlw.getNumberOfLiteralWords())));
    }

    EWAHBoolArrayRawIterator & operator=(const EWAHBoolArrayRawIterator & other) {
        pointer = other.pointer;
        myparent = other.myparent;
        rlw = other.rlw;
        return *this;
    }

    enum {
        verbose = false
    };
    offsetType pointer;
    const vector<uword> * myparent;
    BufferedRunningLengthWord<uword> rlw;
private:

    EWAHBoolArrayRawIterator();
};

template<class uword>
EWAHBoolArrayIterator<uword> EWAHBoolArray<uword>::uncompress() const {
    return EWAHBoolArrayIterator<uword> (buffer);
}

template<class uword>
EWAHBoolArrayRawIterator<uword> EWAHBoolArray<uword>::raw_iterator() const {
    return EWAHBoolArrayRawIterator<uword> (*this);
}

template<class uword>
bool EWAHBoolArray<uword>::operator==(const EWAHBoolArray & x) const {
    if (sizeinbits != x.sizeinbits)
        return false;
    if (buffer.size() != x.buffer.size())
        return false;
    for (offsetType k = 0; k < buffer.size(); ++k)
        if (buffer[k] != x.buffer[k])
            return false;
    return true;
}

template<class uword>
void EWAHBoolArray<uword>::swap(EWAHBoolArray & x) {
    buffer.swap(x.buffer);
    offsetType tmp = x.sizeinbits;
    x.sizeinbits = sizeinbits;
    sizeinbits = tmp;
    tmp = x.lastRLW;
    x.lastRLW = lastRLW;
    lastRLW = tmp;
}

template<class uword>
void EWAHBoolArray<uword>::append(const EWAHBoolArray & x) {
    if (sizeinbits % wordinbits == 0) {
        // hoping for the best?
        sizeinbits += x.sizeinbits;
        ConstRunningLengthWord<uword> lRLW(buffer[lastRLW]);
        if ((lRLW.getRunningLength() == 0) && (lRLW.getNumberOfLiteralWords()
                == 0)) {
            // it could be that the running length word is empty, in such a case,
            // we want to get rid of it!
            assert(lastRLW == buffer.size() - 1);
            lastRLW = x.lastRLW + buffer.size() - 1;
            buffer.resize(buffer.size() - 1);
            buffer.insert(buffer.end(), x.buffer.begin(), x.buffer.end());
        } else {
            lastRLW = x.lastRLW + buffer.size();
            buffer.insert(buffer.end(), x.buffer.begin(), x.buffer.end());
        }
    } else {
        stringstream ss;
        ss
                << "This should really not happen! You are trying to append to a bitmap having a fractional number of words, that is,  "
                << static_cast<int> (sizeinbits)
                << " bits with a word size in bits of "
                << static_cast<int> (wordinbits) << ". ";
        ss << "Size of the bitmap being appended: " << x.sizeinbits << " bits."
                << endl;
        throw invalid_argument(ss.str());
    }
}

template<class uword>
EWAHBoolArrayIterator<uword>::EWAHBoolArrayIterator(
        const vector<uword> & parent) :
    pointer(0), myparent(parent), compressedwords(0), literalwords(0), rl(0),
            lw(0), b(0) {
    if (pointer < myparent.size())
        readNewRunningLengthWord();
}

template<class uword>
void EWAHBoolArrayIterator<uword>::readNewRunningLengthWord() {
    literalwords = 0;
    compressedwords = 0;
    ConstRunningLengthWord<uword> rlw(myparent[pointer]);
    rl = rlw.getRunningLength();
    lw = rlw.getNumberOfLiteralWords();
    b = rlw.getRunningBit();
    if ((rl == 0) && (lw == 0)) {
        if (pointer < myparent.size() - 1) {
            ++pointer;
            readNewRunningLengthWord();
        } else {
            assert(pointer >= myparent.size() - 1);
            pointer = myparent.size();
            assert(!hasNext());
        }
    }
}

template<class uword>
BoolArray<uword> EWAHBoolArray<uword>::toBoolArray() const {
    BoolArray<uword> ans(sizeinbits);
    EWAHBoolArrayIterator<uword> i = uncompress();
    offsetType counter = 0;
    while (i.hasNext()) {
        ans.setWord(counter++, i.next());
    }
    return ans;
}

template<class uword>
template<class container>
void EWAHBoolArray<uword>::appendRowIDs(container & out, const offsetType offset) const {
    offsetType pointer(0);
    offsetType currentoffset(offset);
    if (RESERVEMEMORY)
        out.reserve(buffer.size() + 64);// trading memory for speed.
    while (pointer < buffer.size()) {
        ConstRunningLengthWord<uword> rlw(buffer[pointer]);
        if (rlw.getRunningBit()) {
            for (offsetType x = 0; x < static_cast<offsetType> (rlw.getRunningLength()
                    * wordinbits); ++x) {
                out.push_back(currentoffset + x);
            }
        }
        currentoffset = static_cast<offsetType> (currentoffset
                + rlw.getRunningLength() * wordinbits);
        ++pointer;
        for (uword k = 0; k < rlw.getNumberOfLiteralWords(); ++k) {
            const uword currentword = buffer[pointer];
            for (offsetType kk = 0; kk < wordinbits; ++kk) {
                if ((currentword & static_cast<uword> (static_cast<uword> (1)
                        << kk)) != 0)
                    out.push_back(currentoffset + kk);
            }
            currentoffset += wordinbits;
            ++pointer;
        }
    }
}

template<class uword>
bool EWAHBoolArray<uword>::operator!=(const EWAHBoolArray<uword> & x) const {
    return !(*this == x);
}

template<class uword>
bool EWAHBoolArray<uword>::operator==(const BoolArray<uword> & x) const {
    // could be more efficient
    return (this->toBoolArray() == x);
}

template<class uword>
bool EWAHBoolArray<uword>::operator!=(const BoolArray<uword> & x) const {
    // could be more efficient
    return (this->toBoolArray() != x);
}

template<class uword>
offsetType EWAHBoolArray<uword>::addStreamOfEmptyWords(const bool v, offsetType number) {
    if (number == 0)
        return 0;
    sizeinbits += number * wordinbits;
    offsetType wordsadded = 0;
    if ((RunningLengthWord<uword>::getRunningBit(buffer[lastRLW]) != v)
            && (RunningLengthWord<uword>::size(buffer[lastRLW]) == 0)) {
        RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    } else if ((RunningLengthWord<uword>::getNumberOfLiteralWords(
            buffer[lastRLW]) != 0) || (RunningLengthWord<uword>::getRunningBit(
            buffer[lastRLW]) != v)) {
        buffer.push_back(0);
        ++wordsadded;
        lastRLW = buffer.size() - 1;
        if (v)
            RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    }
    const uword runlen = RunningLengthWord<uword>::getRunningLength(
            buffer[lastRLW]);

    const uword
            whatwecanadd =
                    number
                            < static_cast<offsetType> (RunningLengthWord<uword>::largestrunninglengthcount
                                    - runlen) ? static_cast<uword> (number)
                            : static_cast<uword> (RunningLengthWord<uword>::largestrunninglengthcount
                                    - runlen);
    RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
            static_cast<uword> (runlen + whatwecanadd));

    number -= static_cast<offsetType> (whatwecanadd);
    while (number >= RunningLengthWord<uword>::largestrunninglengthcount) {
        buffer.push_back(0);
        ++wordsadded;
        lastRLW = buffer.size() - 1;
        if (v)
            RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
        RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
                RunningLengthWord<uword>::largestrunninglengthcount);
        number
                -= static_cast<offsetType> (RunningLengthWord<uword>::largestrunninglengthcount);
    }
    if (number > 0) {
        buffer.push_back(0);
        ++wordsadded;
        lastRLW = buffer.size() - 1;
        if (v)
            RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
        RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
                static_cast<uword> (number));
    }
    return wordsadded;
}


template<class uword>
void EWAHBoolArray<uword>::fastaddStreamOfEmptyWords(const bool v, offsetType number) {
    if ((RunningLengthWord<uword>::getRunningBit(buffer[lastRLW]) != v)
            && (RunningLengthWord<uword>::size(buffer[lastRLW]) == 0)) {
        RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    } else if ((RunningLengthWord<uword>::getNumberOfLiteralWords(
            buffer[lastRLW]) != 0) || (RunningLengthWord<uword>::getRunningBit(
            buffer[lastRLW]) != v)) {
        buffer.push_back(0);
        lastRLW = buffer.size() - 1;
        if (v)
            RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
    }
    const uword runlen = RunningLengthWord<uword>::getRunningLength(
            buffer[lastRLW]);

    const uword
            whatwecanadd =
                    number
                            < static_cast<offsetType> (RunningLengthWord<uword>::largestrunninglengthcount
                                    - runlen) ? static_cast<uword> (number)
                            : static_cast<uword> (RunningLengthWord<uword>::largestrunninglengthcount
                                    - runlen);
    RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
            static_cast<uword> (runlen + whatwecanadd));

    number -= static_cast<offsetType> (whatwecanadd);
    while (number >= RunningLengthWord<uword>::largestrunninglengthcount) {
        buffer.push_back(0);
        lastRLW = buffer.size() - 1;
        if (v)
            RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
        RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
                RunningLengthWord<uword>::largestrunninglengthcount);
        number
                -= static_cast<offsetType> (RunningLengthWord<uword>::largestrunninglengthcount);
    }
    if (number > 0) {
        buffer.push_back(0);
        lastRLW = buffer.size() - 1;
        if (v)
            RunningLengthWord<uword>::setRunningBit(buffer[lastRLW], v);
        RunningLengthWord<uword>::setRunningLength(buffer[lastRLW],
                static_cast<uword> (number));
    }
}


template<class uword>
offsetType EWAHBoolArray<uword>::addStreamOfDirtyWords(const uword * v,
        const offsetType number) {
    if (number == 0)
        return 0;
    RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
    const uword NumberOfLiteralWords =
            lastRunningLengthWord.getNumberOfLiteralWords();
    assert(
            RunningLengthWord<uword>::largestliteralcount
                    >= NumberOfLiteralWords);
    const offsetType
            whatwecanadd =
                    number
                            < static_cast<uword> (RunningLengthWord<uword>::largestliteralcount
                                    - NumberOfLiteralWords) ? number
                            : static_cast<offsetType> (RunningLengthWord<uword>::largestliteralcount
                                    - NumberOfLiteralWords);//0x7FFF-NumberOfLiteralWords);
    assert(NumberOfLiteralWords + whatwecanadd >= NumberOfLiteralWords);
    assert(
            NumberOfLiteralWords + whatwecanadd
                    <= RunningLengthWord<uword>::largestliteralcount);
    lastRunningLengthWord.setNumberOfLiteralWords(
            static_cast<uword> (NumberOfLiteralWords + whatwecanadd));
    assert(
            lastRunningLengthWord.getNumberOfLiteralWords()
                    == NumberOfLiteralWords + whatwecanadd);
    const offsetType leftovernumber = number - whatwecanadd;
    // add the dirty words...
    const offsetType oldsize(buffer.size());
    buffer.resize(oldsize + whatwecanadd);
    memcpy(&buffer[oldsize], v, whatwecanadd * sizeof(uword));
	sizeinbits += whatwecanadd * wordinbits;
    offsetType wordsadded(whatwecanadd);
    if (leftovernumber > 0) {
        //add
        buffer.push_back(0);
        lastRLW = buffer.size() - 1;
        ++wordsadded;
        wordsadded += addStreamOfDirtyWords(v + whatwecanadd, leftovernumber);
    }
    assert(wordsadded >= number);
    return wordsadded;
}



template<class uword>
offsetType EWAHBoolArray<uword>::addEmptyWord(const bool v) {
    RunningLengthWord<uword> lastRunningLengthWord(buffer[lastRLW]);
    const bool noliteralword = (lastRunningLengthWord.getNumberOfLiteralWords()
            == 0);
    //first, if the last running length word is empty, we align it
    // this
    uword runlen = lastRunningLengthWord.getRunningLength();
    if ((noliteralword) && (runlen == 0)) {
        lastRunningLengthWord.setRunningBit(v);
        assert(lastRunningLengthWord.getRunningBit() == v);
    }
    if ((noliteralword) && (lastRunningLengthWord.getRunningBit() == v)
            && (runlen < RunningLengthWord<uword>::largestrunninglengthcount)) {
        lastRunningLengthWord.setRunningLength(static_cast<uword> (runlen + 1));
        assert(lastRunningLengthWord.getRunningLength() == runlen + 1);
        return 0;
    } else {
        // we have to start anew
        buffer.push_back(0);
        lastRLW = buffer.size() - 1;
        RunningLengthWord<uword> lastRunningLengthWord2(buffer[lastRLW]);
        assert(lastRunningLengthWord2.getRunningLength() == 0);
        assert(lastRunningLengthWord2.getRunningBit() == 0);
        assert(lastRunningLengthWord2.getNumberOfLiteralWords() == 0);
        lastRunningLengthWord2.setRunningBit(v);
        assert(lastRunningLengthWord2.getRunningBit() == v);
        lastRunningLengthWord2.setRunningLength(1);
        assert(lastRunningLengthWord2.getRunningLength() == 1);
        assert(lastRunningLengthWord2.getNumberOfLiteralWords() == 0);
        return 1;
    }
}
template<class uword>
void EWAHBoolArray<uword>::logicalor(EWAHBoolArray &a, EWAHBoolArray &container) {
    makeSameSize(a);
    container.reset();
    if (RESERVEMEMORY)
        container.buffer.reserve(buffer.size() + a.buffer.size());
    assert(sizeInBits() == a.sizeInBits());
    EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
    EWAHBoolArrayRawIterator<uword> j = raw_iterator();
    if (!(i.hasNext() and j.hasNext())) {// hopefully this never happens...
        container.setSizeInBits(sizeInBits());
        return;
    }
    // at this point, this should be safe:
    BufferedRunningLengthWord<uword> & rlwi = i.next();
    BufferedRunningLengthWord<uword> & rlwj = j.next();
    //RunningLength;
    while (true) {
        bool i_is_prey(rlwi.size() < rlwj.size());
        BufferedRunningLengthWord<uword> & prey(i_is_prey ? rlwi : rlwj);
        BufferedRunningLengthWord<uword> & predator(i_is_prey ? rlwj : rlwi);
        if (prey.getRunningBit() == 0) {
            // we have a stream of 0x00
            const uword predatorrl(predator.getRunningLength());
            const uword preyrl(prey.getRunningLength());
            if (predatorrl >= preyrl) {
                const uword tobediscarded = preyrl;
                container.addStreamOfEmptyWords(predator.getRunningBit(),
                        static_cast<offsetType> (tobediscarded));
            } else {
                const uword tobediscarded = predatorrl;
                container.addStreamOfEmptyWords(predator.getRunningBit(),
                        static_cast<offsetType> (tobediscarded));
                if (preyrl - tobediscarded > 0) {
                    const uword * dw_predator(
                            i_is_prey ? j.dirtyWords() : i.dirtyWords());
                    container.addStreamOfDirtyWords(dw_predator,
                            static_cast<offsetType> (preyrl - tobediscarded));
                }
            }
            predator.discardFirstWords(preyrl);
            prey.discardFirstWords(preyrl);
        } else {
            // we have a stream of 1x11
            const uword preyrl(prey.getRunningLength());
            predator.discardFirstWords(preyrl);
            prey.discardFirstWords(preyrl);
            container.addStreamOfEmptyWords(1, static_cast<offsetType> (preyrl));
        }
        const uword predatorrl(predator.getRunningLength());
        if (predatorrl > 0) {
            if (predator.getRunningBit() == 0) {
                const uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
                const uword tobediscarded =
                        (predatorrl >= nbre_dirty_prey) ? nbre_dirty_prey
                                : predatorrl;
                if (tobediscarded > 0) {
                    const uword * dw_prey(
                            i_is_prey ? i.dirtyWords() : j.dirtyWords());
                    container.addStreamOfDirtyWords(dw_prey,
                            static_cast<offsetType> (tobediscarded));
                    predator.discardFirstWords(tobediscarded);
                    prey.discardFirstWords(tobediscarded);
                }
            } else {
                const uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
                const uword tobediscarded =
                        (predatorrl >= nbre_dirty_prey) ? nbre_dirty_prey
                                : predatorrl;
                predator.discardFirstWords(tobediscarded);
                prey.discardFirstWords(tobediscarded);
                container.addStreamOfEmptyWords(1,
                        static_cast<offsetType> (tobediscarded));
            }
        }
        assert(prey.getRunningLength() == 0);
        // all that is left to do now is to AND the dirty words
        uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
        if (nbre_dirty_prey > 0) {
            assert(predator.getRunningLength() == 0);
            const uword * idirty = i.dirtyWords();
            const uword * jdirty = j.dirtyWords();
            for (uword k = 0; k < nbre_dirty_prey; ++k) {
                container.addWord(static_cast<uword>(idirty[k] | jdirty[k]));
            }
            predator.discardFirstWords(nbre_dirty_prey);
        }
        if (i_is_prey) {
            if (!i.hasNext())
                break;
            rlwi = i.next();
        } else {
            if (!j.hasNext())
                break;
            rlwj = j.next();
        }
    }
    container.setSizeInBits(sizeInBits());
}

template<class uword>
void EWAHBoolArray<uword>::logicalxor(EWAHBoolArray &a, EWAHBoolArray &container) {
    makeSameSize(a);
    container.reset();
    if (RESERVEMEMORY)
        container.buffer.reserve(buffer.size() + a.buffer.size());
    assert(sizeInBits() == a.sizeInBits());
    EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
    EWAHBoolArrayRawIterator<uword> j = raw_iterator();
    if (!(i.hasNext() and j.hasNext())) {// hopefully this never happens...
        container.setSizeInBits(sizeInBits());
        return;
    }
    // at this point, this should be safe:
    BufferedRunningLengthWord<uword> & rlwi = i.next();
    BufferedRunningLengthWord<uword> & rlwj = j.next();
    //RunningLength;
    while (true) {
        bool i_is_prey(rlwi.size() < rlwj.size());
		BufferedRunningLengthWord<uword> & prey(i_is_prey ? rlwi : rlwj);
		BufferedRunningLengthWord<uword> & predator(i_is_prey ? rlwj : rlwi);
		uword predatorrl(predator.getRunningLength());
		const uword preyrl(prey.getRunningLength());
        if (predatorrl >= preyrl) {
			const uword tobediscarded = preyrl;
			container.addStreamOfEmptyWords(
					prey.getRunningBit() ^ predator.getRunningBit(),
					static_cast<offsetType> (tobediscarded));
		} else {
			assert(predatorrl<preyrl);
			const uword tobediscarded = predatorrl;
			if(predatorrl>0) {
				container.addStreamOfEmptyWords(
					prey.getRunningBit() ^ predator.getRunningBit(),
					static_cast<offsetType> (predatorrl));
			}
			if (preyrl - tobediscarded > 0) {
				const uword * dw_predator(
						i_is_prey ? j.dirtyWords() : i.dirtyWords());
				if (prey.getRunningBit() == 0) {
					container.addStreamOfDirtyWords(dw_predator,
							static_cast<offsetType> (preyrl - tobediscarded));
				} else {
					for(offsetType x = 0; x<static_cast<offsetType> (preyrl - tobediscarded);++x)
								container.addWord(static_cast<uword>(~dw_predator[x]));
				}
			}
		}
		predator.discardFirstWords(preyrl);
		prey.discardFirstWords(preyrl);

		predatorrl = predator.getRunningLength();
		if (predatorrl > 0) {

			const uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
			const uword tobediscarded =
					(predatorrl >= nbre_dirty_prey) ? nbre_dirty_prey
							: predatorrl;
			if (tobediscarded > 0) {
				const uword * dw_prey(
						i_is_prey ? i.dirtyWords() : j.dirtyWords());
				if (predator.getRunningBit() == 0) {
					container.addStreamOfDirtyWords(dw_prey,
							static_cast<offsetType> (tobediscarded));
				} else {
					for(offsetType x = 0; x<tobediscarded;++x)
						container.addWord(static_cast<uword>(~dw_prey[x]));
				}
				predator.discardFirstWords(tobediscarded);
				prey.discardFirstWords(tobediscarded);
			}
		}
		assert(prey.getRunningLength() == 0);
        // all that is left to do now is to AND the dirty words
        uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
        if (nbre_dirty_prey > 0) {
            assert(predator.getRunningLength() == 0);
            const uword * idirty = i.dirtyWords();
            const uword * jdirty = j.dirtyWords();

            for (uword k = 0; k < nbre_dirty_prey; ++k) {
                container.addWord(idirty[k] ^ jdirty[k]);
            }
            predator.discardFirstWords(nbre_dirty_prey);
        }
        if (i_is_prey) {
            if (!i.hasNext())
                break;
            rlwi = i.next();
        } else {
            if (!j.hasNext())
                break;
            rlwj = j.next();
        }
    }
    container.setSizeInBits(sizeInBits());
}


template<class uword>
void EWAHBoolArray<uword>::logicaland(EWAHBoolArray &a,
        EWAHBoolArray &container) {
    makeSameSize(a);
    container.reset();
    if (RESERVEMEMORY)
        container.buffer.reserve(
                buffer.size() > a.buffer.size() ? buffer.size()
                        : a.buffer.size());
    assert(sizeInBits() == a.sizeInBits());
    EWAHBoolArrayRawIterator<uword> i = a.raw_iterator();
    EWAHBoolArrayRawIterator<uword> j = raw_iterator();
    if (!(i.hasNext() and j.hasNext())) {// hopefully this never happens...
        container.setSizeInBits(sizeInBits());
        return;
    }
    // at this point, this should be safe:
    BufferedRunningLengthWord<uword> & rlwi = i.next();
    BufferedRunningLengthWord<uword> & rlwj = j.next();
    //RunningLength;
    while (true) {
        bool i_is_prey(rlwi.size() < rlwj.size());
        BufferedRunningLengthWord<uword> & prey(i_is_prey ? rlwi : rlwj);
        BufferedRunningLengthWord<uword> & predator(i_is_prey ? rlwj : rlwi);
        if (prey.getRunningBit() == 0) {
            const uword preyrl(prey.getRunningLength());
            predator.discardFirstWords(preyrl);
            prey.discardFirstWords(preyrl);
            container.addStreamOfEmptyWords(0, static_cast<offsetType> (preyrl));
        } else {
            // we have a stream of 1x11
            const uword predatorrl(predator.getRunningLength());
            const uword preyrl(prey.getRunningLength());
            const uword tobediscarded = (predatorrl >= preyrl) ? preyrl
                    : predatorrl;
            container.addStreamOfEmptyWords(predator.getRunningBit(),
                    static_cast<offsetType> (tobediscarded));
            if (preyrl - tobediscarded > 0) {
                const uword * dw_predator(
                        i_is_prey ? j.dirtyWords() : i.dirtyWords());
                container.addStreamOfDirtyWords(dw_predator,
                        static_cast<offsetType> (preyrl - tobediscarded));
            }
            predator.discardFirstWords(preyrl);
            prey.discardFirstWords(preyrl);
        }
        const uword predatorrl(predator.getRunningLength());
        if (predatorrl > 0) {
            if (predator.getRunningBit() == 0) {
                const uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
                const uword tobediscarded =
                        (predatorrl >= nbre_dirty_prey) ? nbre_dirty_prey
                                : predatorrl;
                predator.discardFirstWords(tobediscarded);
                prey.discardFirstWords(tobediscarded);
                container.addStreamOfEmptyWords(0,
                        static_cast<offsetType> (tobediscarded));
            } else {
                const uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
                const uword tobediscarded =
                        (predatorrl >= nbre_dirty_prey) ? nbre_dirty_prey
                                : predatorrl;
                if (tobediscarded > 0) {
                    const uword * dw_prey(
                            i_is_prey ? i.dirtyWords() : j.dirtyWords());
                    container.addStreamOfDirtyWords(dw_prey,
                            static_cast<offsetType> (tobediscarded));
                    predator.discardFirstWords(tobediscarded);
                    prey.discardFirstWords(tobediscarded);
                }
            }
        }
        assert(prey.getRunningLength() == 0);
        // all that is left to do now is to AND the dirty words
        uword nbre_dirty_prey(prey.getNumberOfLiteralWords());
        if (nbre_dirty_prey > 0) {
            assert(predator.getRunningLength() == 0);
            const uword * idirty = i.dirtyWords();
            const uword * jdirty = j.dirtyWords();
            for (uword k = 0; k < nbre_dirty_prey; ++k) {
                container.addWord(static_cast<uword>(idirty[k] & jdirty[k]));
            }
            predator.discardFirstWords(nbre_dirty_prey);
        }
        if (i_is_prey) {
            if (!i.hasNext())
                break;
            rlwi = i.next();
        } else {
            if (!j.hasNext())
                break;
            rlwj = j.next();
        }
    }
    container.setSizeInBits(sizeInBits());
}

template<class uword>
BitmapStatistics EWAHBoolArray<uword>::computeStatistics() const {
    //uint totalcompressed(0), totalliteral(0);
    BitmapStatistics bs;
    EWAHBoolArrayRawIterator<uword> i = raw_iterator();
    while (i.hasNext()) {
        BufferedRunningLengthWord<uword> &brlw(i.next());
        ++bs.runningwordmarker;
        bs.totalliteral += brlw.getNumberOfLiteralWords();
        bs.totalcompressed += brlw.getRunningLength();
        if (brlw.getRunningLength()
                == RunningLengthWord<uword>::largestrunninglengthcount) {
            ++bs.maximumofrunningcounterreached;
        }
    }
    return bs;
}

template<class uword>
void EWAHBoolArray<uword>::debugprintout() const {
    cout << "==printing out EWAHBoolArray==" << endl;
    cout << "Number of compressed words: " << buffer.size() << endl;
    offsetType pointer = 0;
    while (pointer < buffer.size()) {
        ConstRunningLengthWord<uword> rlw(buffer[pointer]);
        bool b = rlw.getRunningBit();
        const uword rl = rlw.getRunningLength();
        const uword lw = rlw.getNumberOfLiteralWords();
        cout << "pointer = " << pointer << " running bit=" << b
                << " running length=" << rl << " lit. words=" << lw << endl;
        for (uword j = 0; j < lw; ++j) {
            const uword & w = buffer[pointer + j + 1];
            cout << toBinaryString(w) << endl;
            ;
        }
        pointer += lw + 1;
    }
    cout << "==END==" << endl;
}

template<class uword>
offsetType EWAHBoolArray<uword>::sizeOnDisk() const {
    return sizeof(sizeinbits) + sizeof(offsetType) + sizeof(uword) * buffer.size();
}

#endif
