#!/usr/bin/env python
"""Train HMMs for alignment of signal data from the MinION
"""
from __future__ import print_function, division
import sys
import h5py as h5
sys.path.append("../")
from multiprocessing import Process, Queue, current_process, Manager
from nanoporeLib import *
from argparse import ArgumentParser
from random import shuffle


def parse_args():
    parser = ArgumentParser (description=__doc__)

    parser.add_argument('--file_directory', '-d', action='append', default=None,
                        dest='files_dir', required=False, type=str,
                        help="directories with fast5 files to train on")
    parser.add_argument('--ref', '-r', action='store', default=None,
                        dest='ref', required=False, type=str,
                        help="location of refrerence sequence in FASTA")
    parser.add_argument('--output_location', '-o', action='store', dest='out', default=None,
                        required=False, type=str,
                        help="directory to put the trained model, and use for working directory.")
    parser.add_argument('--iterations', '-i', action='store', dest='iter', default=10,
                        required=False, type=int)
    parser.add_argument('--train_amount', '-a', action='store', dest='amount', default=15,
                        required=False, type=int,
                        help="limit the total length of sequence to use in training.")
    parser.add_argument('--diagonalExpansion', '-e', action='store', dest='diag_expansion', type=int,
                        required=False, default=None, help="number of diagonals to expand around each anchor")
    parser.add_argument('--constraintTrim', '-m', action='store', dest='constraint_trim', type=int,
                        required=False, default=None, help='amount to remove from an anchor constraint')
    parser.add_argument('--threshold', '-t', action='store', dest='threshold', type=float, required=False,
                        default=0.01, help="posterior match probability threshold")
    parser.add_argument('--in_template_hmm', '-T', action='store', dest='in_T_Hmm',
                        required=False, type=str, default=None,
                        help="input HMM for template events, if you don't want the default")
    parser.add_argument('--in_complement_hmm', '-C', action='store', dest='in_C_Hmm',
                        required=False, type=str, default=None,
                        help="input HMM for complement events, if you don't want the default")
    parser.add_argument('--un-banded', '-ub', action='store_true', dest='banded',
                        default=False, help='flag, use banded alignment heuristic')
    parser.add_argument('--jobs', '-j', action='store', dest='nb_jobs', required=False, default=4,
                        type=int, help="number of jobs to run concurrently")
    parser.add_argument('--stateMachineType', '-smt', action='store', dest='stateMachineType', type=str,
                        default="threeState", required=False,
                        help="StateMachine options: threeState, threeStateHdp, vanilla")

    parser.add_argument('--buildHdp', action='store', dest='buildHDP', default=None,
                        help="Build Hdp, specify type, options: "
                             "singleLevelFixed, singleLevelPrior, multisetFixed, multisetPrior")
    parser.add_argument('--buildAlignments', '-al', action='store', dest='buildAlignments', default=None)
    parser.add_argument('--templateHDP', '-tH', action='store', dest='templateHDP', default=None)
    parser.add_argument('--complementHDP', '-cH', action='store', dest='complementHDP', default=None)
    parser.add_argument('--mutated_reference', '-mr', action='append', default=None,
                        dest='mut_ref', required=False, type=str,
                        help="mutated reference sequence to train on")
    args = parser.parse_args()
    return args


def get_2d_length(fast5):
    read = h5.File(fast5, 'r')
    read_length = 0
    twoD_read_sequence_address = "/Analyses/Basecall_2D_000/BaseCalled_2D/Fastq"
    if not (twoD_read_sequence_address in read):
        print("This read didn't have a 2D read", fast5, end='\n', file=sys.stderr)
        read.close()
        return 0
    else:
        read_length = len(read[twoD_read_sequence_address][()].split()[2])
        read.close()
        return read_length


def cull_training_files(directories, training_amount):
    print("trainModels - culling training files.\n", end="", file=sys.stderr)

    training_files = []

    for directory in directories:
        fast5s = [x for x in os.listdir(directory) if x.endswith(".fast5")]
        shuffle(fast5s)

        total_amount = 0
        n = 0
        for i in xrange(len(fast5s)):
            training_files.append(directory + fast5s[i])
            n += 1
            total_amount += get_2d_length(directory + fast5s[i])
            if total_amount >= training_amount:
                break
        print("Culled {nb_files} training files from {dir}.".format(nb_files=n,
                                                                    dir=directory),
              end="\n", file=sys.stderr)

    return training_files


def get_expectations(work_queue, done_queue):
    try:
        for f in iter(work_queue.get, 'STOP'):
            alignment = SignalAlignment(**f)
            alignment.run(get_expectations=True)
    except Exception, e:
        done_queue.put("%s failed with %s" % (current_process().name, e.message))


def get_model(type, symbol_set_size, threshold):
    assert (type in ["threeState", "vanilla", "threeStateHdp"]), "Unsupported StateMachine type"
    if type == "threeState":
        return ContinuousPairHmm(model_type=type, symbol_set_size=symbol_set_size)
    if type == "vanilla":
        return ConditionalSignalHmm(model_type=type, symbol_set_size=symbol_set_size)
    if type == "threeStateHdp":
        return HdpSignalHmm(model_type=type, symbol_set_size=symbol_set_size, threshold=threshold)


def add_and_norm_expectations(path, files, model, hmm_file):
    model.likelihood = 0
    for f in files:
        model.add_expectations_file(path + f)
        os.remove(path + f)
    model.normalize()
    model.write(hmm_file)
    model.running_likelihoods.append(model.likelihood)
    if type(model) is HdpSignalHmm:
        model.reset_assignments()


def build_hdp(hdp_type, template_hdp_path, complement_hdp_path, alignments,
              template_assignments=None, complement_assignments=None):
    def get_hdp_type(requested_type):
        hdp_types = {
            "singleLevelFixed": 0,
            "singleLevelPrior": 1,
            "multisetFixed": 2,
            "multisetPrior": 3,
        }
        assert (requested_type in hdp_types.keys()), "Requested HDP type is invalid, got {}".format(requested_type)
        return hdp_types[requested_type]

    if alignments is not None:
        assert (hdp_type is not None), "Need to specify HDP type"
        start_message = """\n
    Building Nanopore HDP
    Using Alignments from {alignments}
    Putting template HDP here: {tHdp}
    Putting complement HDP here: {cHdp}
    Making Type: {type}
    """.format(alignments=alignments, tHdp=template_hdp_path, cHdp=complement_hdp_path, type=hdp_type)
        print(start_message)
        command = "./vanillaAlign --buildHDP -p {type} -v {tHdpP} -w {cHdpP} -a {al}".format(
            type=get_hdp_type(hdp_type), tHdpP=template_hdp_path, cHdpP=complement_hdp_path, al=alignments
        )
        os.system(command)
        print("\ntrainModels - Finished Building HDP\n")
        sys.exit(0)
    elif (template_assignments is not None) and (complement_assignments is not None):
        command = "./vanillaAlign --buildHDP -v {tHdpP} -w {cHdpP}" \
                  " -t {tExpectations} -c {cExpectations}".format(tHdpP=template_hdp_path,
                                                                  cHdpP=complement_hdp_path,
                                                                  tExpectations=template_assignments,
                                                                  cExpectations=complement_assignments)
        os.system(command)
        print("trainModels - built HDP.", file=sys.stderr)
        return
    else:
        print("trainModels - illegal BuildHDP options\n", file=sys.stderr)
        sys.exit(1)


def main(argv):
    # parse command line arguments
    args = parse_args()

    # build the HDP if that's what we're doing
    if args.buildHDP is not None:
        assert (None not in [args.buildHDP, args.templateHDP, args.complementHDP, args.buildAlignments])
        build_hdp(hdp_type=args.buildHDP, template_hdp_path=args.templateHDP, complement_hdp_path=args.complementHDP,
                  alignments=args.buildAlignments)

    start_message = """\n
    Starting Baum-Welch training.
    Directories with training files: {files_dir}
    Training on {amount} bases.
    Using reference sequence: {ref}
    Input template/complement models: {inTHmm}/{inCHmm}
    Writing trained models to: {outLoc}
    Performing {iterations} iterations.
    Using model: {model}
    \n
    """.format(files_dir=args.files_dir, amount=args.amount, ref=args.ref,
               inTHmm=args.in_T_Hmm, inCHmm=args.in_C_Hmm, outLoc=args.out,
               iterations=args.iter, model=args.stateMachineType)

    assert (args.files_dir is not None), "Need to specify which files to train on"
    assert (args.ref is not None), "Need to provide a reference file"
    assert (args.out is not None), "Need to know the working directory for training"

    print(start_message, file=sys.stdout)

    if not os.path.isfile(args.ref):  # TODO make this is_fasta(args.ref)
        print("Did not find valid reference file", file=sys.stderr)
        sys.exit(1)

    # make directory to put the files we're using files
    working_folder = FolderHandler()
    working_directory_path = working_folder.open_folder(args.out + "tempFiles_expectations")
    reference_seq = working_folder.add_file_path("reference_seq.txt")
    make_temp_sequence(args.ref, True, reference_seq)

    # index the reference for bwa
    print("signalAlign - indexing reference", file=sys.stderr)
    bwa_ref_index = get_bwa_index(args.ref, working_directory_path)
    print("signalAlign - indexing reference, done", file=sys.stderr)

    # make model objects, these handle normalizing, loading, and writing
    template_model = get_model(type=args.stateMachineType, symbol_set_size=4096, threshold=args.threshold)
    complement_model = get_model(type=args.stateMachineType, symbol_set_size=4096, threshold=args.threshold)

    # get the input HDP, if we're using it
    if args.stateMachineType == "threeStateHdp":
        assert (args.templateHDP is not None) and (args.complementHDP is not None), "Need to provide serialized HDP " \
                                                                                    "files for this stateMachineType"
        assert (os.path.isfile(args.templateHDP)) and (os.path.isfile(args.complementHDP)), "Could not find the HDP" \
                                                                                            "files"

    # make some paths to files to hold the HMMs
    template_hmm = working_folder.add_file_path("template_trained.hmm")
    complement_hmm = working_folder.add_file_path("complement_trained.hmm")

    print("Starting {iterations} iterations.\n\n\t    Running likelihoods\ni\tTempalte\tComplement".format(
        iterations=args.iter), file=sys.stdout)

    for i in xrange(args.iter):
        # if we're starting there are no HMMs
        if i == 0:
            in_template_hmm = None
            in_complement_hmm = None
        else:
            in_template_hmm = template_hmm
            in_complement_hmm = complement_hmm

        # first cull a set of files to get expectations on
        training_files = cull_training_files(args.files_dir, args.amount)

        # setup
        workers = args.nb_jobs
        work_queue = Manager().Queue()
        done_queue = Manager().Queue()
        jobs = []

        # get expectations for all the files in the queue
        for fast5 in training_files:
            alignment_args = {
                "in_fast5": fast5,
                "reference": reference_seq,
                "destination": working_directory_path,
                "stateMachineType": args.stateMachineType,
                "banded": args.banded,
                "bwa_index": bwa_ref_index,
                "in_templateHmm": in_template_hmm,
                "in_complementHmm": in_complement_hmm,
                "in_templateHdp": args.templateHDP,
                "in_complementHdp": args.complementHDP,
                "threshold": args.threshold,
                "diagonal_expansion": args.diag_expansion,
                "constraint_trim": args.constraint_trim,
            }
            #alignment = SignalAlignment(**alignment_args)
            #alignment.run(get_expectations=True)
            work_queue.put(alignment_args)

        for w in xrange(workers):
            p = Process(target=get_expectations, args=(work_queue, done_queue))
            p.start()
            jobs.append(p)
            work_queue.put('STOP')

        for p in jobs:
            p.join()

        done_queue.put('STOP')

        # load then normalize the expectations
        template_expectations_files = [x for x in os.listdir(working_directory_path)
                                       if x.endswith(".template.expectations")]

        complement_expectations_files = [x for x in os.listdir(working_directory_path)
                                         if x.endswith(".complement.expectations")]

        if len(template_expectations_files) > 0:
            add_and_norm_expectations(path=working_directory_path,
                                      files=template_expectations_files,
                                      model=template_model,
                                      hmm_file=template_hmm)

        if len(complement_expectations_files) > 0:
            add_and_norm_expectations(path=working_directory_path,
                                      files=complement_expectations_files,
                                      model=complement_model,
                                      hmm_file=complement_hmm)
        # Build HDP from last round of assignments
        if args.stateMachineType == "threeStateHdp":
            build_hdp(hdp_type=None, template_hdp_path=args.templateHDP, complement_hdp_path=args.complementHDP,
                      alignments=None, template_assignments=template_hmm, complement_assignments=complement_hmm)

        if len(template_model.running_likelihoods) > 0 and len(complement_model.running_likelihoods) > 0:
            print("{i}| {t_likelihood}\t{c_likelihood}".format(t_likelihood=template_model.running_likelihoods[-1],
                                                               c_likelihood=complement_model.running_likelihoods[-1],
                                                               i=i))

    # if we're using HDP, trim the final Hmm (remove assignments)

    print("trainModels - finished training routine", file=sys.stdout)
    print("trainModels - finished training routine", file=sys.stderr)


if __name__ == "__main__":
    sys.exit(main(sys.argv))

