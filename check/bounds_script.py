#!/usr/bin/env python3.4

import sys
import os
import argparse
from subprocess import call

import glob
import re
import subprocess

import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

from matplotlib.backends.backend_pdf import PdfPages

from matplotlib import cm

import matplotlib.ticker as mticker

import math

from matplotlib import gridspec





DIR, FILENAME = os.path.split(__file__)
params = {}
TEMPNAME = 'temp_{}'


def parse_arguments(args):
    """
    Parse the command-line arguments
    :param args: Command-line arguments provided during execution
    :return: Parsed argumetns
    """
    parser = argparse.ArgumentParser()
    parser.add_argument('-o', '--outdir', type=str,
                        default="plots",
                        help='Arguments to be passed on to the performance profiler')
    parser.add_argument('filename', nargs='+',
                        help='Name of the files to be used for the creating the bound plots')
    parsed_args = parser.parse_args(args)
    return parsed_args

def set_params(args):
    """
    Set the global parameters from the parsed command-line arguments
    :param args: parsed command-line arguments
    :return:
    """
    params['outdir'] = args.outdir

def generate_files(files):
    """
    Parse the files and generate temporary files containing only problem name and execution time
    :param files: List of files to be parsed
    :return: A list of all the generated files to be deleted after performance profiling
    """
    for file in files:
        # file = os.path.join(DIR, filename)
        with open(file) as _file:
            df=pd.DataFrame()
            dfvar=pd.DataFrame()
            orig = False
            name = None
            rootbounds = False
            vardetails = False
            settings = 'default'
            for line in _file:
                if line.startswith("loaded parameter file"):
                    settings=line.split()[-1]
                    #settings=settings[3]
                    settings=settings.split("/")[-1]
                    settings = os.path.splitext(settings)[0]
                elif line.startswith("Original Program statistics:"):
                    orig = True
                elif line.startswith("Master Program statistics:"):
                    orig = False
                elif line.startswith("Presolved Problem  :"):
                    orig = False
                elif orig and line.startswith("  Problem name     :"):
                    name = line.split()[3]      
                    name = name.split("/")[-1]
                    tmp_name = name.split(".")[-1]
                    if tmp_name[-1] == "gz" or tmp_name[-1] == "z" or tmp_name[-1] == "GZ" or tmp_name[-1] == "Z":
                        name = os.path.splitext(name)[0]
                    name = os.path.splitext(name)[0]
                    print name
                elif line.startswith("Root bounds"):
                    rootbounds = True
                elif line.startswith("iter	pb	db") and rootbounds:
                    line_array = line.split()
                    df = pd.DataFrame(columns = line_array, dtype = float)
                elif line.startswith("Pricing Summary:") and rootbounds:
                    rootbounds = False
                elif rootbounds:
                    line_array = line.split()
                    df.loc[line_array[0]] = line_array
                elif line.startswith("AddedVarDetails:"):
                    vardetails = True
                elif line.startswith("VAR: name	node	time") and vardetails:
                    line_array = line.split()
                    dfvar = pd.DataFrame(columns = line_array[1:], dtype = float)
                elif line.startswith("Root node:") and vardetails:
                    vardetails = False

                    dfvar = dfvar.set_index('name')

                    dfvar=dfvar.astype(float)

                    df['nlpvars'] = 0

                    for i in range(len(df)):
                        df.set_value(str(i), 'nlpvars', len(dfvar[(dfvar['rootlpsolval'] > 0) & (dfvar['rootredcostcall'] == i)]))

                    df['nipvars'] = 0

                    for i in range(len(df)):
                        df.set_value(str(i), 'nipvars', len(dfvar[(dfvar['solval'] > 0) & (dfvar['rootredcostcall'] == i)]))


                    #df = df.set_index('iter')

                    
                    if df.empty:
                        continue
                    
                    df=df.astype(float)
                    
#                    fig, ax = plt.subplots()
#                    ax2, ax3 = ax.twinx(), ax.twinx()
#                    ax3.set_frame_on(True)
#                    ax3.patch.set_visible(False)
#                    fig.subplots_adjust(right=0.75)

#                    ax = df.plot(kind='line', y='pb', color='red', label='pb', ax=ax);
#                    ax = df.plot(kind='line', y='db', color='blue', label='db', ax=ax);                    
#                    df.plot(kind='bar', y='nlpvars', color='purple', label='nlpvars', ax=ax3, secondary_y=True, alpha=0.2);
#                    df.plot(kind='bar', y='dualdiff', color='green', label='dualdiff', ax=ax2, secondary_y=True, alpha=0.5);
                    
                    infty = 10.0 ** 20
                    df['db'][df['db'] <= -infty] = np.nan                    


                    #df['nlpvars'][df['nlpvars'] <= 0.5] = np.nan
                    #df['nipvars'][df['nipvars'] <= 0.5] = np.nan
                    
                    lpmax = df['nlpvars'].max()
                    ipmax = df['nipvars'].max()
                    
                    if( np.isnan(ipmax) ):
                        ipmax = 0.0    

                    if( np.isnan(lpmax) ):
                        lpmax = 0.0    
                    
#                    pd.set_option('display.max_rows', len(df))
#                    print df
#                    pd.reset_option('display.max_rows')

                    
                    
                    #fig = plt.figure(figsize=(8, 6)) 
                    gs = gridspec.GridSpec(3, 1, height_ratios=[3, 1, 1]) 
                    ax = plt.subplot(gs[0])
                    ax1 = plt.subplot(gs[1])
                    ax2 = plt.subplot(gs[2])

#                    ax0 = plt.gca()
#                    myLocator = mticker.MultipleLocator(10 ** (math.floor(math.log10(len(df.index)))))
#                    ax0.xaxis.set_major_locator(plt.NullFormatter())
                    
                    #ax1 = df.plot(kind='bar', y=['nlpvars','nipvars'], color=['blue','red'], linewidth=0, label=['nlpvars','nipvars'], ax=ax1, secondary_y=False);
                    
                    ax1.set_ylim(bottom=0.0, top=lpmax+1)  
                    ax1 = df.plot(kind='scatter', x='iter', y='nlpvars', color='blue', label='nlpvars', ax=ax1, secondary_y=False, s=6);
                    ax1.set_xticklabels([])
                    x_axis = ax1.axes.get_xaxis()
                    x_axis.set_label_text('')
                    #x_axis = ax1.axes.get_xaxis()
                    x_axis.set_visible(False)                    
                    base = 10 ** (math.floor(math.log10(len(df.index))))
                    ax2.set_ylim(bottom=0.0, top=ipmax+1)
                    ax2 = df.plot(kind='scatter', x='iter', y='nipvars', color='red', label='nipvars', ax=ax2, secondary_y=False, s=6);
                    myLocator = mticker.MultipleLocator(base)
                    majorFormatter = mticker.FormatStrFormatter('%d')
#                    myLocator2 = mticker.MultipleLocator(1)
                    ax2.xaxis.set_major_locator(myLocator)
                    ax2.xaxis.set_major_formatter(majorFormatter)
                    ax2.xaxis.set_minor_locator(plt.NullLocator())                
                    
                    ax.set_xticklabels([])
                    x_axis = ax.axes.get_xaxis()
                    x_axis.set_label_text('')
                    #x_axis = ax1.axes.get_xaxis()
                    x_axis.set_visible(False)

                    #ax = None
                    ax = df.plot(kind='line', y='pb', color='red', label='pb', ax=ax);
                    ax = df.plot(kind='line', y='db', color='blue', label='db', ax=ax);
                    ax = df.plot(kind='scatter', x='iter', y='db', color='blue', label=None, ax=ax, s=1);
                    ax = df.plot(kind='line', y='dualdiff', color='green', label='dualdiff', ax=ax, secondary_y=True, alpha=0.25);
                    
                    plt.savefig(params['outdir']+"/"+name+"_"+settings+".png")
                    
                elif vardetails:
                    line_array = line.split()
                    dfvar.loc[line_array[1]] = line_array[1:]


                    
                               

def main():
    """Entry point when calling this script"""
    args = sys.argv[1:]
    parsed_args = parse_arguments(args)
    set_params(parsed_args)
    if not os.path.exists(params['outdir']):
        os.makedirs(params['outdir'])
    generate_files(parsed_args.filename)

# Calling main script
if __name__ == '__main__':
    main()
