import numpy as np

def outlier_filter(datas, threshold = 1):
    datas = np.array(datas)
    z = np.abs((datas - datas.mean(axis = 0)) / datas.std(axis = 0))
    m = datas.mean(axis = 0)
    
    for i in m:
        print("{:.0f}".format(i))

    # TODO: maybe use numpy function
    for i in range(z.shape[0]):
        for j in range(z.shape[1]):
            if z[i, j] > threshold:
                datas[i, j] = m[j]
    return datas

if __name__ == '__main__':
    f = open('out.txt', 'r+')
    lines = f.readlines()
    num_case = len(lines[0].split())
    
    datas = []
    for line in lines:
        _list = [int(e) for e in line.split()]
        datas.append(_list)
    
    datas = outlier_filter(datas)

    np.savetxt('out.txt', datas, fmt='%d')
    f.close()
