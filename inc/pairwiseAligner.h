/*
 * Copyright (C) 2009-2011 by Benedict Paten (benedictpaten@gmail.com)
 *
 * Released under the MIT license, see LICENSE.txt
 */

/*
 * pairwiseAligner.h
 *
 *  Created on: 5 Jul 2010
 *      Author: benedictpaten
 */

#ifndef PAIRWISEALIGNER_H_
#define PAIRWISEALIGNER_H_

#include "bioioC.h"
#include "sonLib.h"
#include "stateMachine.h"
#include "sonLibTypes.h"

//The exception string
extern const char *PAIRWISE_ALIGNMENT_EXCEPTION_ID;

//Constant that gives the integer value equal to probability 1. Integer probability zero is always 0.
#define PAIR_ALIGNMENT_PROB_1 10000000

//Sequence
typedef enum {
    nucleotide=0,
    kmer=1,
    event=2
} SequenceType;

typedef struct _sequence Sequence;

struct _sequence {
    int64_t length;
    void *elements;
    void *(*get)(void *elements, int64_t index);
    Sequence *(*sliceFcn)(Sequence *, int64_t, int64_t);
};

/*
 * Sequence constructor function
 * stringLength should be the length of the sequence in bases ie ATGAC has
 * length 5.  *elements is a pointer to the char array, typically.  The
 * SequenceType is 0-nucleotide, 1-kmer, 2-event
*/
Sequence *sequence_construct(int64_t length, void *elements, void *(*getFcn)(void *, int64_t));

Sequence *sequence_construct2(int64_t length, void *elements, void *(*getFcn)(void *, int64_t),
                              Sequence *(*sliceFcn)(Sequence *, int64_t, int64_t));


void sequence_padSequence(Sequence *sequence);

//slice a sequence object
Sequence *sequence_sliceNucleotideSequence2(Sequence *inputSequence, int64_t start, int64_t sliceLength);

Sequence *sequence_sliceEventSequence2(Sequence *inputSequence, int64_t start, int64_t sliceLength);

void sequence_sequenceDestroy(Sequence *seq);

void *sequence_getBase(void *elements, int64_t index);

void *sequence_getKmer(void *elements, int64_t index);

// get the kmer at index and the previous kmer
void *sequence_getKmer2(void *elements, int64_t index);

// for HDP, different 'NULL'
void *sequence_getKmer3(void *elements, int64_t index);

void *sequence_getEvent(void *elements, int64_t index);

int64_t sequence_correctSeqLength(int64_t length, SequenceType type);

// Pairwise alignment
typedef struct _pairwiseAlignmentBandingParameters {
    double threshold; //Minimum posterior probability of a match to be added to the output
    int64_t minDiagsBetweenTraceBack; //Minimum x+y diagonals to leave between doing traceback.
    int64_t traceBackDiagonals; //Number of diagonals to leave between trace back diagonal
    int64_t diagonalExpansion; //The number of x-y diagonals to expand around an anchor point
    int64_t constraintDiagonalTrim; //Amount to remove from a diagonal to be considered for a banding constraint
    int64_t anchorMatrixBiggerThanThis; //Search for anchors on any matrix bigger than this
    int64_t repeatMaskMatrixBiggerThanThis; //Any matrix in the anchors bigger than this is searched for anchors using non-repeat masked sequences.
    int64_t splitMatrixBiggerThanThis; //Any matrix in the anchors bigger than this is split into two.
    bool alignAmbiguityCharacters;
    float gapGamma; //The AMAP gap-gamma parameter which controls the degree to which indel probabilities are factored into the alignment.
} PairwiseAlignmentParameters;

PairwiseAlignmentParameters *pairwiseAlignmentBandingParameters_construct();

void pairwiseAlignmentBandingParameters_destruct(PairwiseAlignmentParameters *p);

/*
 * Gets the set of posterior match probabilities under a simple HMM model of alignment for two DNA sequences.
 */
stList *getAlignedPairs(StateMachine *sM, void *cX, void *cY, int64_t lX, int64_t lY,
                        PairwiseAlignmentParameters *p,
                        void *(*getXFcn)(void *, int64_t),
                        void *(*getYFcn)(void *, int64_t),
                        stList *(*getAnchorPairFcn)(void *, void *, PairwiseAlignmentParameters *),
                        bool alignmentHasRaggedLeftEnd, bool alignmentHasRaggedRightEnd);

typedef struct PairwiseAlignment PairwiseAlignment; // added to remove invisibility warning

stList *convertPairwiseForwardStrandAlignmentToAnchorPairs(PairwiseAlignment *pA, int64_t trim);

/*
 * Expectation calculation functions for EM algorithms.
 */
void cell_updateExpectations(double *fromCells, double *toCells, int64_t from, int64_t to, double eP, double tP,
                             void *extraArgs);

void cell_signal_updateTransAndKmerSkipExpectations(double *fromCells, double *toCells, int64_t from, int64_t to,
                                                    double eP, double tP, void *extraArgs);

void cell_signal_updateTransAndKmerSkipExpectations2(double *fromCells, double *toCells, int64_t from, int64_t to,
                                                     double eP, double tP, void *extraArgs);

void cell_signal_updateBetaAndAlphaProb(double *fromCells, double *toCells, int64_t from, int64_t to, double eP,
                                        double tP,
                                        void *extraArgs);

void getExpectations(StateMachine *sM, Hmm *hmmExpectations,
                     void *sX, void *sY, int64_t lX, int64_t lY, PairwiseAlignmentParameters *p,
                     void *(getFcn)(void *, int64_t),
                     stList *(*getAnchorPairFcn)(void *, void *, PairwiseAlignmentParameters *),
                     bool alignmentHasRaggedLeftEnd, bool alignmentHasRaggedRightEnd);

/*
 * Methods tested and possibly useful elsewhere
 */

////Diagonal

typedef struct _diagonal {
    int64_t xay; //x + y coordinate
    int64_t xmyL; //smallest x - y coordinate
    int64_t xmyR; //largest x - y coordinate
} Diagonal;

Diagonal diagonal_construct(int64_t xay, int64_t xmyL, int64_t xmyR);

int64_t diagonal_getXay(Diagonal diagonal);

int64_t diagonal_getMinXmy(Diagonal diagonal);

int64_t diagonal_getMaxXmy(Diagonal diagonal);

int64_t diagonal_getWidth(Diagonal diagonal);

int64_t diagonal_getXCoordinate(int64_t xay, int64_t xmy);

int64_t diagonal_getYCoordinate(int64_t xay, int64_t xmy);

int64_t diagonal_equals(Diagonal diagonal1, Diagonal diagonal2);

char *diagonal_getString(Diagonal diagonal);

//Band

typedef struct _band Band;

Band *band_construct(stList *anchorPairs, int64_t lX, int64_t lY,
        int64_t expansion);

void band_destruct(Band *band);

////Band iterator.

typedef struct _bandIterator BandIterator;

BandIterator *bandIterator_construct(Band *band);

void bandIterator_destruct(BandIterator *bandIterator);

BandIterator *bandIterator_clone(BandIterator *bandIterator);

Diagonal bandIterator_getNext(BandIterator *bandIterator);

Diagonal bandIterator_getPrevious(BandIterator *bandIterator);

//Log add

#define LOG_ZERO -INFINITY

double logAdd(double x, double y);


//Cell calculations

void cell_calculateForward(StateMachine *sM, double *current, double *lower, double *middle, double *upper, void* cX, void* cY, void *extraArgs);

void cell_calculateBackward(StateMachine *sM, double *current, double *lower, double *middle, double *upper, void* cX, void* cY, void *extraArgs);

double cell_dotProduct(double *cell1, double *cell2, int64_t stateNumber);

double cell_dotProduct2(double *cell1, StateMachine *sM, double (*getStateValue)(StateMachine *, int64_t));

//DpDiagonal

typedef struct _dpDiagonal DpDiagonal;

DpDiagonal *dpDiagonal_construct(Diagonal diagonal, int64_t stateNumber);

DpDiagonal *dpDiagonal_clone(DpDiagonal *diagonal);

bool dpDiagonal_equals(DpDiagonal *diagonal1, DpDiagonal *diagonal2);

void dpDiagonal_destruct(DpDiagonal *dpDiagonal);

double *dpDiagonal_getCell(DpDiagonal *dpDiagonal, int64_t xmy);

double dpDiagonal_dotProduct(DpDiagonal *diagonal1, DpDiagonal *diagonal2);

void dpDiagonal_zeroValues(DpDiagonal *diagonal);

void dpDiagonal_initialiseValues(DpDiagonal *diagonal, StateMachine *sM,
                                 double (*getStateValue)(StateMachine *, int64_t));

//DpMatrix

typedef struct _dpMatrix DpMatrix;

DpMatrix *dpMatrix_construct(int64_t diagonalNumber, int64_t stateNumber);

void dpMatrix_destruct(DpMatrix *dpMatrix);

DpDiagonal *dpMatrix_getDiagonal(DpMatrix *dpMatrix, int64_t xay);

int64_t dpMatrix_getActiveDiagonalNumber(DpMatrix *dpMatrix);

DpDiagonal *dpMatrix_createDiagonal(DpMatrix *dpMatrix, Diagonal diagonal);

void dpMatrix_deleteDiagonal(DpMatrix *dpMatrix, int64_t xay);

//Diagonal calculations

void diagonalCalculationForward(StateMachine *sM, int64_t xay, DpMatrix *dpMatrix, Sequence* sX, Sequence* sY);

void diagonalCalculationBackward(StateMachine *sM, int64_t xay, DpMatrix *dpMatrix, Sequence* sX, Sequence* sY);

double diagonalCalculationTotalProbability(StateMachine *sM, int64_t xay, DpMatrix *forwardDpMatrix,
                                           DpMatrix *backwardDpMatrix, Sequence* sX, Sequence* sY);

void diagonalCalculationPosteriorMatchProbs(StateMachine *sM, int64_t xay, DpMatrix *forwardDpMatrix,
                                            DpMatrix *backwardDpMatrix, Sequence* sX, Sequence* sY,
                                            double totalProbability, PairwiseAlignmentParameters *p,
                                            void *extraArgs);

void diagonalCalculationMultiPosteriorMatchProbs(StateMachine *sM, int64_t xay, DpMatrix *forwardDpMatrix,
                                                 DpMatrix *backwardDpMatrix, Sequence* sX, Sequence* sY,
                                                 double totalProbability, PairwiseAlignmentParameters *p,
                                                 void *extraArgs);

void diagonalCalculationExpectations(StateMachine *sM, int64_t xay,
                                     DpMatrix *forwardDpMatrix, DpMatrix *backwardDpMatrix, Sequence* sX, Sequence* sY,
                                     double totalProbability, PairwiseAlignmentParameters *p, void *extraArgs);

void diagonalCalculation_Expectations(StateMachine *sM, int64_t xay,
                                      DpMatrix *forwardDpMatrix, DpMatrix *backwardDpMatrix,
                                      Sequence *sX, Sequence *sY,
                                      double totalProbability,
                                      PairwiseAlignmentParameters *p, void *extraArgs);

void getPosteriorProbsWithBanding(StateMachine *sM,
                                  stList *anchorPairs,
                                  Sequence* sX, Sequence* sY,
                                  PairwiseAlignmentParameters *p,
                                  bool alignmentHasRaggedLeftEnd, bool alignmentHasRaggedRightEnd,
                                  void (*diagonalPosteriorProbFn)(StateMachine *, int64_t, DpMatrix *, DpMatrix *,
                                                                  Sequence*, Sequence*,
                                                                  double, PairwiseAlignmentParameters *, void *),
                                  void *extraArgs);

stList *getAlignedPairsWithoutBanding(StateMachine *sM, void *cX, void *cY, int64_t lX, int64_t lY,
                                      PairwiseAlignmentParameters *p,
                                      void *(*getXFcn)(void *, int64_t),
                                      void *(*getYFcn)(void *, int64_t),
                                      void (*diagonalPosteriorProbFn)(StateMachine *, int64_t, DpMatrix *,
                                                                      DpMatrix *, Sequence *, Sequence *, double,
                                                                      PairwiseAlignmentParameters *, void *),
                                      bool alignmentHasRaggedLeftEnd, bool alignmentHasRaggedRightEnd);

stList *getAlignedPairsUsingAnchors(StateMachine *sM,
                                    Sequence *SsX, Sequence *SsY,
                                    stList *anchorPairs,
                                    PairwiseAlignmentParameters *p,
                                    void (*diagonalPosteriorProbFn)(StateMachine *, int64_t, DpMatrix *,
                                                                    DpMatrix *, Sequence *, Sequence *, double,
                                                                    PairwiseAlignmentParameters *, void *),
                                    bool alignmentHasRaggedLeftEnd,
                                    bool alignmentHasRaggedRightEnd);

// EM stuff
void getExpectationsUsingAnchors(StateMachine *sM, Hmm *hmmExpectations,
                                 Sequence *SsX, Sequence *SsY,
                                 stList *anchorPairs,
                                 PairwiseAlignmentParameters *p,
                                 void (*diagonalCalcExpectationFcn)(StateMachine *sM, int64_t xay,
                                                                    DpMatrix *forwardDpMatrix,
                                                                    DpMatrix *backwardDpMatrix,
                                                                    Sequence* sX, Sequence* sY,
                                                                    double totalProbability,
                                                                    PairwiseAlignmentParameters *p,
                                                                    void *extraArgs),
                                 bool alignmentHasRaggedLeftEnd,
                                 bool alignmentHasRaggedRightEnd);


//Blast pairs

int sortByXPlusYCoordinate(const void *i, const void *j);

int sortByXPlusYCoordinate2(const void *i, const void *j);

stList *getBlastPairs(const char *sX, const char *sY, int64_t trim, bool repeatMask);

stList *getBlastPairsForPairwiseAlignmentParameters(void *sX, void *sY, PairwiseAlignmentParameters *p);

stList *filterToRemoveOverlap(stList *overlappingPairs);

//Split over large gaps

stList *getSplitPoints(stList *anchorPairs, int64_t lX, int64_t lY,
        int64_t maxMatrixSize, bool alignmentHasRaggedLeftEnd, bool alignmentHasRaggedRightEnd);

void getPosteriorProbsWithBandingSplittingAlignmentsByLargeGaps(
        StateMachine *sM, stList *anchorPairs, Sequence *SsX, Sequence *SsY,
        PairwiseAlignmentParameters *p,
        bool alignmentHasRaggedLeftEnd, bool alignmentHasRaggedRightEnd,
        void (*diagonalPosteriorProbFn)(StateMachine *, int64_t, DpMatrix *,
                                        DpMatrix *, Sequence*, Sequence*, double,
                                        PairwiseAlignmentParameters *, void *),
        void (*coordinateCorrectionFn)(), void *extraArgs);

//Calculate posterior probabilities of being aligned to gaps

int64_t *getIndelProbabilities(stList *alignedPairs, int64_t seqLength, bool xIfTrueElseY);

//Does reweighting, destroys input aligned pairs in the process.
stList *reweightAlignedPairs(stList *alignedPairs,
        int64_t *indelProbsX, int64_t *indelProbsY, double gapGamma);

stList *reweightAlignedPairs2(stList *alignedPairs, int64_t seqLengthX, int64_t seqLengthY, double gapGamma);

#endif /* PAIRWISEALIGNER_H_ */
