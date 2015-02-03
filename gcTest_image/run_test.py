from subprocess import Popen, PIPE, STDOUT
#from pyparsing import *
from time import time
from os import system
import re
import svgwrite
from svgwrite import cm, mm   
from colorsys import hls_to_rgb

title = "| Название |Всего затрачено времени (c)| время потраченное сборщиком (c)| количество сборок | кол-во аллокаций\
 до сборки| высвобождение памяти в минуту |средняя длительность паузы (с)| средний размер кучи (кб)|\
 средний размер свободной памяти (кб)| процент эффективно занятой памяти\n"


def write_float(f):
    if f > 100 and f < 100000:
        return "{:n}".format(f)
    elif f > 0.001:
        return "{:.3}".format(f)
    else:
        return "{:.2e}".format(f)

def writeTable(data, filename):
    f = open(filename, 'w')
    f.write('[cols="^h,9*", options="header,footer"]\n')
    f.write("|===\n")
    f.write(title)
    for testname, gclist in data.items():
        f.write("10+| [white]#" + testname + "#\n")
        f.write("{set:cellbgcolor:navy}")
        for gc in gclist:
            for gc_characteristic in gc:
                if type(gc_characteristic) is float:
                    f.write("| " + write_float(gc_characteristic))
                else:
                    f.write("| " + str(gc_characteristic))
                f.write("{set:cellbgcolor:white}")
            f.write("\n")
    f.write(title)
    f.write("|===")
    f.close()
    
def write_svg_color(color):
    r,g,b = color
    return 'rgb(' + str(int(r*255)) + ',' + str(int(g*255)) + ',' + str(int(b*255)) + ')'

def draw_dispertion(dwg, d, x, y, w):
    line_w = 0.5
    line_color = "rgb(250,0,0)"
    start = x - d
    if start < 0: start = 0
    dwg.add(dwg.line(start=(start*mm, (y+2)*mm), end=(start*mm, (y+w-2)*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=(start*mm, (y+w/2)*mm), end=((x+d)*mm, (y+w/2)*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=((x+d)*mm, (y+2)*mm), end=((x+d)*mm, (y+w-2)*mm), stroke=line_color, stroke_width=line_w*mm))

def draw_chart(name, gc_names, time_stats, heap_stats, pause_stats):
    accent_colors = ["rgb(161,55,74)", "rgb(79,67,117)", "rgb(157,180,186)", "rgb(246,219,105)", "rgb(244,108,62)"]
    colors =["rgb(204,70,95)","rgb(101,86,150)", "rgb(183,210,217)","rgb(255,227,168)", "rgb(255,113,97)"]
    first_plot_title = "Время потраченное сборщиком /"
    first_plot_title2 = "Общее время (с)"
    second_plot_title = "Использованная память /"
    second_plot_title2 = " Общая выделенная память (Kb)"
    third_plot_title = "средняя длительность паузы (с)"
    third_plot_title2 = "+ дисперсия длительности паузы"
    header_style = "font-size:16px; font-weight:bold;  font-family:Noto Serif"
    header_color = "rgb(0,0,0)"
    gc_names_style = "font-size:20px; font-weight:bold;  font-family:Noto Serif;text-anchor:end"
    gc_names_color = "rgb(0,0,0)"
    subheader_style = "font-size:20px; font-weight:bold;  font-family:Noto Serif; text-anchor:middle"
    subheader_color = "rgb(255,255,255)"
    metric_color = "rgb(0,0,0)"
    metric_style = "font-size:15px; font-weight:bold;  font-family:Noto Serif;"
    metric_end_style = "font-size:14px; font-weight:bold;  font-family:Noto Serif; text-anchor:end"
    bar_width = 10
    ypos = ypos_start = 0
    xpos = 0
    plot_w = 100
    title_colon_w = 70
    line_w = 0.4
    line_color = "rgb(190,190,190)"
    total_w = title_colon_w + plot_w*3 + line_w*5
    header_h = 14
    subheader_height = 8
    colontitul_h = 5
    total_h = len(time_stats)*(subheader_height + colontitul_h + len(gc_names)*(line_w + bar_width)) + header_h + line_w*2
    dwg = svgwrite.Drawing(filename=name, size=(total_w*mm, total_h*mm))
    #table border
    dwg.add(dwg.line(start=(xpos*mm, ypos*mm), end=((total_w+xpos)*mm, ypos*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=(xpos*mm, ypos*mm), end=(xpos*mm, (ypos+total_h)*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=(xpos*mm, (ypos+total_h)*mm), end=((total_w+xpos)*mm, (ypos+total_h)*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=((total_w+xpos)*mm, ypos*mm), end=((total_w+xpos)*mm, (ypos+total_h)*mm), stroke=line_color, stroke_width=line_w*mm))
    #column divider lines
    dwg.add(dwg.line(start=((xpos+title_colon_w+line_w)*mm, ypos*mm), end=((xpos+title_colon_w+line_w)*mm, (ypos+total_h)*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=((xpos+title_colon_w+plot_w+line_w*2)*mm, ypos*mm), end=((xpos+title_colon_w+plot_w+line_w*2)*mm, (ypos+total_h)*mm), stroke=line_color, stroke_width=line_w*mm))
    dwg.add(dwg.line(start=((xpos+title_colon_w+plot_w*2+line_w*3)*mm, ypos*mm), end=((xpos+title_colon_w+plot_w*2+line_w*3)*mm, (ypos+total_h)*mm), stroke=line_color, stroke_width=line_w*mm))
    #table title
    dwg.add(dwg.text(first_plot_title, insert=((title_colon_w+10+xpos)*mm, (ypos+5)*mm), fill=header_color, style = header_style))    
    dwg.add(dwg.text(first_plot_title2, insert=((title_colon_w+10+xpos)*mm, (ypos+10)*mm), fill=header_color, style = header_style))    
    dwg.add(dwg.text(second_plot_title, insert=((title_colon_w+plot_w+10+xpos)*mm, (ypos+5)*mm), fill=header_color, style = header_style))
    dwg.add(dwg.text(second_plot_title2, insert=((title_colon_w+plot_w+10+xpos)*mm, (ypos+10)*mm), fill=header_color, style = header_style))
    dwg.add(dwg.text(third_plot_title, insert=((title_colon_w+2*plot_w+10+xpos)*mm, (ypos+5)*mm), fill=header_color, style = header_style))
    dwg.add(dwg.text(third_plot_title2, insert=((title_colon_w+2*plot_w+10+xpos)*mm, (ypos+10)*mm), fill=header_color, style = header_style))
    ypos += header_h + line_w
    for testname in time_stats: #each table

        real_w = max([x[1] for x in time_stats[testname]])
        scale = (plot_w-5)/real_w
        real_w2 = max([x[1] for x in heap_stats[testname]])
        scale2 = (plot_w-5)/real_w2
        real_w3 = max([x[1] + x[0] for x in pause_stats[testname]])
        scale3 = (plot_w-5)/real_w3
        step = 0
        dwg.add(dwg.rect(insert=(xpos*mm, ypos*mm), size=(total_w*mm, (subheader_height)*mm), fill='navy'))
        ypos += subheader_height
        dwg.add(dwg.text(testname, insert=((total_w/2)*mm, (ypos-2)*mm), fill=subheader_color, style=subheader_style))
        for i in range(len(time_stats[testname])): #each bar
            w1, w2 = time_stats[testname][i]
            w3, w4 = heap_stats[testname][i]
            w5, d5 = pause_stats[testname][i]
            step ^= 1
            xpos += title_colon_w+line_w*2
            dwg.add(dwg.rect(insert=(xpos*mm, ypos*mm), size=((w2*scale)*mm, bar_width*mm), fill=colors[i]))
            dwg.add(dwg.rect(insert=((xpos+plot_w+line_w)*mm, ypos*mm), size=((w4*scale2)*mm, bar_width*mm), fill=colors[i]))

            svgcolor = write_svg_color(hls_to_rgb((185 + ypos)/360 + step*0.75, 0.3, 0.7))
            dwg.add(dwg.rect(insert=(xpos*mm, ypos*mm), size=((w1*scale)*mm, bar_width*mm), fill=accent_colors[i]))
            dwg.add(dwg.rect(insert=((xpos+plot_w+line_w)*mm, ypos*mm), size=((w3*scale2)*mm, bar_width*mm), fill=accent_colors[i]))
            xpos += plot_w*2 + line_w*2
            dwg.add(dwg.rect(insert=(xpos*mm, ypos*mm), size=((w5*scale3)*mm, bar_width*mm), fill=colors[i]))
            draw_dispertion(dwg, d5*scale3, xpos + w5*scale3, ypos, bar_width)
            xpos -= plot_w*2 + title_colon_w + line_w*4
            ypos += bar_width
            dwg.add(dwg.text(gc_names[i], insert=((title_colon_w-10)*mm, (ypos-4)*mm), fill=gc_names_color, style = gc_names_style))    
            dwg.add(dwg.line(start=(xpos*mm, ypos*mm), end=((total_w+xpos)*mm, ypos*mm), stroke=line_color, stroke_width=line_w*mm))
            ypos += line_w/2
        ypos += colontitul_h - 1
        dwg.add(dwg.text("0", insert=((title_colon_w+5)*mm, (ypos)*mm), fill=metric_color, style = metric_style))    
        dwg.add(dwg.text(write_float(real_w), insert=((title_colon_w+plot_w-5)*mm, (ypos)*mm), fill=metric_color, style = metric_end_style))    
        dwg.add(dwg.text("0", insert=((title_colon_w + plot_w + 5)*mm, (ypos)*mm), fill=metric_color, style = metric_style))    
        dwg.add(dwg.text(write_float(real_w2), insert=((title_colon_w + plot_w*2 - 5)*mm, (ypos)*mm), fill=metric_color, style = metric_end_style))    
        dwg.add(dwg.text("0", insert=((title_colon_w + plot_w*2 + 5)*mm, (ypos)*mm), fill=metric_color, style = metric_style))    
        dwg.add(dwg.text(write_float(real_w3), insert=((title_colon_w + plot_w*3 - 5)*mm, (ypos)*mm), fill=metric_color, style = metric_end_style))    
        ypos += 1
    #bars = dwg.add(dwg.g(id='shapes', fill='red'))
        
    #dwg.add(dwg.line(start=((plot_w+xpos)*mm, ypos_start*mm), end=((plot_w+xpos)*mm, ypos*mm), stroke='black'))

    # set presentation attributes at object creation as SVG-Attributes

    # or set presentation attributes by helper functions of the Presentation-Mixin
    dwg.save()


    
#def makeBarChart(data, filename):

state = ""

def makeMarkSweepLLVM():
    global state
    if state != 'llvm':
        state = 'llvm'
        system (b'cd ../buildLLVM \n\
                  rm CMakeCache.txt\n\
                  cmake -DLLVM=ON ..\n\
                  make               \n\
                  cd ../gcTest_image')
    return Popen(['../buildLLVM/llst', './LittleSmalltalk.image'], stdout=PIPE, stdin=PIPE, stderr=STDOUT)

def makeNonCollecting():
    global state
    if state != 'no-llvm':
        state = 'no-llvm'
        system (b'cd ../build\n\
                  rm CMakeCache.txt\n\
                  cmake ..\n\
                  make \n\
                  cd ../gcTest_image')
    return Popen(['../build/llst', './LittleSmalltalk.image','--mm_type','nc'], stdout=PIPE, stdin=PIPE, stderr=STDOUT)


def makeMarkSweep():
    global state
    if state != 'no-llvm':
        state = 'no-llvm'
        system (b'cd ../build\n\
                  rm CMakeCache.txt\n\
                  cmake ..\n\
                  make \n\
                  cd ../gcTest_image')
    return Popen(['../build/llst', './LittleSmalltalk.image'], stdout=PIPE, stdin=PIPE, stderr=STDOUT)

#should return subprocess
gc = [
    ('copy_LLVM', makeMarkSweepLLVM),
    ('copy', makeMarkSweep),
    ('non_collecting', makeNonCollecting)
    #('non_collecting', lambda: Popen(['../build/llst', './LittleSmalltalk.image','-m','nc'], stdout=PIPE, stdin=PIPE, stderr=STDOUT)),
]
#bigAndManySmall.
#tests = ['listWithHoles', 'listWithArrays', 'treeTest', 'treeTestWithBackLinks', 'diff']
tests = ['listWithHoles', 'listWithArrays', 'treeTest', 'treeTestWithBackLinks', 'diff']
results = dict() 
heap_stats = dict() 
time_stats = dict()
pause_stats = dict()
for test in tests:    
    heap_stats[test] = []
    results[test] = []
    time_stats[test] = []
    pause_stats[test] = []

csv_pattern = '\w+;\s*([\d+\s]+[,[\d+\s]+]?);(\s*(\w|%)+)'
gc_out_pattern = b'GC count: (\d+) \(\d+/\d+\), average allocations per gc: (\d+), microseconds spent in GC: (\d+)'
heap_out_pattern = b'Heap size \(Kb\*s\): (\d+), Free heap size \(Kb\*s\): (\d+)'
#compile
for gcName, makeFun in gc:
    for test in tests:    
        p = makeFun() 
        #create subrocess for current test execution
        
        folder          = ('./' + gcName + '/' + test + '/').encode()
        _t              = time()
        stdout          = p.communicate(input=b'GCStressTest new ' + test.encode() + b'\n\0')[0]
        system(b'mkdir -p ' + folder)
        system(b'echo "' + stdout + b'" > ' + folder + b'stdout.log')
        total_delay     = time() - _t
        parsed          = re.search(gc_out_pattern ,stdout.splitlines()[-3])
        gc_count        = int(parsed.group(1))
        gc_aver_alloc   = int(parsed.group(2))
        gc_delay        = int(parsed.group(3)) / 1000000.0
        parsed          = re.search(heap_out_pattern, stdout.splitlines()[-2])
        aver_heap       = int(parsed.group(1)) / total_delay
        aver_free_heap  = int(parsed.group(2)) / total_delay
        heap_stats[test].append((aver_heap - aver_free_heap, aver_heap))
        time_stats[test].append((gc_delay, total_delay))
        my_throughput   = float(aver_heap - aver_free_heap) / aver_heap
        freedPerMin = avgPause = throughput = 0.0
        system(b'cp ./gc.log ' + folder)
        system(b'java -jar gcviewer-1.34-SNAPSHOT.jar gc.log ' + folder + b'log.csv ' + folder + b'plot.png')
        for line in open(folder + b"log.csv",'r'):
            parsed = re.search(csv_pattern, line)
            if (line.startswith('freedMemoryPerMin;')):
                freedPerMin = parsed.group(1) + parsed.group(2)
            if (line.startswith('avgPause;')):
                avgPause = float(parsed.group(1).replace(',','.'))
            if (line.startswith('avgPauseσ;')):
                dPause = float(parsed.group(1).replace(',','.'))
            if (line.startswith('throughput;')):
                throughput = parsed.group(1) + parsed.group(2)
            if (line.startswith('accumPause;')):
                gc_delay = float(parsed.group(1).replace(',','.'))
        pause_stats[test].append((avgPause, dPause))
        results[test].append([gcName, total_delay, gc_delay, gc_count, gc_aver_alloc, 
            freedPerMin, avgPause, aver_heap, aver_free_heap, "{:.2}".format(my_throughput)])
writeTable(results, "results.adoc")
draw_chart('chart.svg', [x[0] for x in gc], time_stats, heap_stats, pause_stats)
system(b'~/.gem/ruby/2.1.0/bin/asciidoctor results.adoc') #TODO set PATH
#system(b'~/.gem/ruby/2.1.0/bin/asciidoctor -b pdf results.adoc')
#charactestics:
#V total delay| V memory delay | V count | V average allocations | V average freed memory per minute 
# | V average pause per collect | V aver_heap | V aver_free_heap | V throughput (процент полезного времени)
