import svgwrite
from svgwrite import cm, mm   
from colorsys import hls_to_rgb

def write_svg_color(color):
    r,g,b = color
    return 'rgb(' + str(int(r*255)) + ',' + str(int(g*255)) + ',' + str(int(b*255)) + ')'



def draw_chart(name):
    first_plot_title = "Время потраченное сборщиком | Общее время (с)"
    second_plot_title = "Использованная память | Общая выделенная память (Kb)"
    third_plot_title = "средняя пауза (+дисперсия) (с)"

    header_style = "font-size:20px; font-weight:bold;  font-family:Noto Serif"
    header_color = "rgb(255,255,255)"
    gc_names_style = "font-size:20px; font-weight:bold;  font-family:Noto Serif"
    gc_names_color = "rgb(255,255,255)"
    subheader_style = "font-size:20px; font-weight:bold;  font-family:Noto Serif"
    subheader_color = "rgb(255,255,255)"
    bar_width = 10
    dwg = svgwrite.Drawing(filename=name, debug=True)
    ypos = ypos_start = 40
    xpos = 60
    plot_w = 100
    title_colon_w = 100
    line_w = 1
    line_color = 'black'
    total_w = title_colon_w + plot_w*2 + line_w*4
    header_h = 10
    subheader_height = 7
    total_h = len(time_stats)*(subheader_height + len(gc_names)*(line_w + bar_width)) + header_h + line_w*2
    #table border
    dwg.add(dwg.line(start=(xpos*mm, ypos*mm), end=((total_w+xpos)*mm, ypos*mm), stroke=line_color))
    dwg.add(dwg.line(start=(xpos*mm, ypos*mm), end=(xpos*mm, (ypos+total_h)*mm), stroke=line_color))
    dwg.add(dwg.line(start=(xpos*mm, (ypos+total_h)*mm), end=((total_w+xpos)*mm, (ypos+total_h)*mm), stroke=line_color))
    dwg.add(dwg.line(start=((total_w+xpos)*mm, ypos*mm), end=((total_w+xpos)*mm, (ypos+total_h)*mm), stroke=line_color))
    #table title
    dwg.add(dwg.text(first_plot_title, insert=((title_colon_w+20+xpos)*mm, (ypos+2)*mm), fill=header_color, style = header_style))    
    dwg.add(dwg.text(second_plot_title, insert=((title_colon_w+plot_w+20+xpos)*mm, (ypos+2)*mm), fill=header_color, style = header_style))
    ypos += header_h + line_w
    for testname in time_stats: #each table

        real_w = max([x[1] for x in time_stats[testname]])
        scale = plot_w/real_w
        real_w2 = max([x[1] for x in heap_stats[testname]])
        scale2 = plot_w/real_w2
        step = 0
        dwg.add(dwg.rect(insert=(10*mm, ypos*mm), size=((2*plot_w + 50)*mm, (subheader_height)*mm), fill='navy'))
        ypos += subheader_height
        dwg.add(dwg.text(testname, insert=((80+xpos)*mm, (ypos-2)*mm), fill='rgb(255,255,255)', 
              style = "font-size:20px; font-weight:bold;  font-family:Noto Serif"))
        for i in range(len(time_stats)-1): #each bar
            w1, w2 = time_stats[testname][i]
            w3, w4 = heap_stats[testname][i]
            step ^= 1
            svgcolor = write_svg_color(hls_to_rgb((185 + ypos)/360 + step*0.75, 0.5, 0.7))
            dwg.add(dwg.rect(insert=(xpos*mm, ypos*mm), size=((w2*scale)*mm, bar_width*mm), fill=svgcolor))
            dwg.add(dwg.rect(insert=((xpos+plot_w)*mm, ypos*mm), size=((w4*scale2)*mm, bar_width*mm), fill=svgcolor))

            svgcolor = write_svg_color(hls_to_rgb((185 + ypos)/360 + step*0.75, 0.3, 0.7))
            dwg.add(dwg.rect(insert=(xpos*mm, ypos*mm), size=((w1*scale)*mm, bar_width*mm), fill=svgcolor))
            dwg.add(dwg.rect(insert=((xpos+plot_w)*mm, ypos*mm), size=((w3*scale2)*mm, bar_width*mm), fill=svgcolor))
            ypos += bar_width
            dwg.add(dwg.text('сэмпл текст', insert=(10*mm, (ypos-4)*mm), fill='rgb(100,200,200)', 
                stroke='rgb(0,100,100)', stroke_width=1,  style = "font-size:20px;  font-family:Noto Serif"))    
            dwg.add(dwg.line(start=(xpos*mm, ypos*mm), end=((total_w+xpos)*mm, ypos*mm), stroke=line_color))
            ypos +=line_w
    #bars = dwg.add(dwg.g(id='shapes', fill='red'))
        
    #dwg.add(dwg.line(start=((plot_w+xpos)*mm, ypos_start*mm), end=((plot_w+xpos)*mm, ypos*mm), stroke='black'))

    # set presentation attributes at object creation as SVG-Attributes

    # or set presentation attributes by helper functions of the Presentation-Mixin
    dwg.save()


draw_chart('chart.svg')